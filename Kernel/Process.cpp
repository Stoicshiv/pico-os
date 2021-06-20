#include <Std/OwnPtr.hpp>

#include <Kernel/Process.hpp>
#include <Kernel/Threads/Scheduler.hpp>
#include <Kernel/Loader.hpp>
#include <Kernel/Interface/System.hpp>
#include <Kernel/FileSystem/FlashFileSystem.hpp>
#include <Kernel/FileSystem/MemoryFileSystem.hpp>

namespace Kernel
{
    Process& Process::active()
    {
        auto& thread = Scheduler::the().active();
        return thread.m_process.must();
    }

    Process& Process::create(StringView name, ElfWrapper elf)
    {
        Vector<String> arguments;
        arguments.append(name);

        Vector<String> variables;

        return Process::create(name, elf, arguments, variables);
    }
    Process& Process::create(StringView name, ElfWrapper elf, const Vector<String>& arguments, const Vector<String>& variables)
    {
        auto process = Process::construct(name);

        auto thread = Thread::construct(String::format("Process: {}", name));

        thread->m_process = process;

        // FIXME: Is this still required?
        thread->m_privileged = true;

        thread->setup_context([arguments, variables, name, elf]() mutable {
            dbgln("Loading executable for process '{}' from {}", name, elf.base());

            auto& process = Process::active();

            process.m_executable = load_executable_into_memory(elf);
            auto& executable = process.m_executable.must();

            StackWrapper stack { { (u8*)executable.m_stack_base, executable.m_stack_size } };

            auto push_cstring_array = [&stack] (const Vector<String>& array) {
                Vector<char*> pointers;
                for (auto& value : array.iter()) {
                    pointers.append(stack.push_cstring(value.cstring()));
                }
                char **pointer = (char**)stack.push_value(nullptr);
                for (size_t i = 0; i < pointers.size(); ++i) {
                    pointer = stack.push_value(pointers[pointers.size() - i - 1]);
                }

                return pointer;
            };

            int argc = arguments.size();
            char **argv = push_cstring_array(arguments);
            char **envp = push_cstring_array(variables);

            dbgln("[Process::create] argv:");
            for (char **value = argv; *value; ++value) {
                dbgln("  {}: {}", *value, StringView { *value });
            }

            dbgln("[Process::create] envp:");
            for (char **value = envp; *value; ++value) {
                dbgln("  {}: {}", *value, StringView { *value });
            }

            auto& thread = Scheduler::the().active();

            VERIFY(__builtin_popcount(executable.m_writable_size) == 1);
            VERIFY(executable.m_writable_base % executable.m_writable_size == 0);
            auto& ram_region = thread.m_regions.append({});
            ram_region.rbar.region = 0;
            ram_region.rbar.valid = 0;
            ram_region.rbar.addr = executable.m_writable_base >> 5;
            ram_region.rasr.enable = 1;
            ram_region.rasr.size = MPU::compute_size(executable.m_writable_size);
            ram_region.rasr.srd = 0b00000000;
            ram_region.rasr.attrs_b = 1;
            ram_region.rasr.attrs_c = 1;
            ram_region.rasr.attrs_s = 1;
            ram_region.rasr.attrs_tex = 0b000;
            ram_region.rasr.attrs_ap = 0b011;
            ram_region.rasr.attrs_xn = 1;

            dbgln("[Process::create] ram_region.rbar={}", ram_region.rbar.raw);

            auto& rom_region = thread.m_regions.append({});
            rom_region.rbar.region = 0;
            rom_region.rbar.valid = 0;
            rom_region.rbar.addr = 0x00000000 >> 5;
            rom_region.rasr.enable = 1;
            rom_region.rasr.size = 13;
            rom_region.rasr.srd = 0b00000000;
            rom_region.rasr.attrs_b = 1;
            rom_region.rasr.attrs_c = 1;
            rom_region.rasr.attrs_s = 1;
            rom_region.rasr.attrs_tex = 0b000;
            rom_region.rasr.attrs_ap = 0b111;
            rom_region.rasr.attrs_xn = 0;

            dbgln("[Process::create] rom_region.rbar={}", rom_region.rbar.raw);

            dbgln("Handing over execution to process '{}' at {}", name, process.m_executable.must().m_entry);
            dbgln("  Got argv={} and envp={}", argv, envp);

            hand_over_to_loaded_executable(process.m_executable.must(), stack, thread.m_regions, argc, argv, envp);

            VERIFY_NOT_REACHED();
        });

        Scheduler::the().add_thread(thread);

        return *process;
    }

    i32 Process::sys$fstat(i32 fd, UserlandFileInfo *statbuf)
    {
        dbgln("[Process::sys$fstat] fd={} statbuf={}", fd, statbuf);

        auto& handle = get_file_handle(fd);

        // FIXME: For device files this will be incorrect
        auto& file = handle.file();

        statbuf->st_dev = file.m_filesystem;
        statbuf->st_rdev = file.m_device_id;
        statbuf->st_size = file.m_size;
        statbuf->st_blksize = 0xdead;
        statbuf->st_blocks = 0xdead;

        statbuf->st_ino = file.m_ino;
        statbuf->st_mode = file.m_mode;
        statbuf->st_uid = file.m_owning_user;
        statbuf->st_gid = file.m_owning_group;

        return 0;
    }

    i32 Process::sys$wait(i32 *status)
    {
        if (m_terminated_children.size() > 0) {
            auto terminated_child_process = m_terminated_children.dequeue();
            *status = terminated_child_process.m_status;

            return terminated_child_process.m_process_id;
        }

        FIXME();
        // Scheduler::the().donate_my_remaining_cpu_slice();

        return -EINTR;
    }

    i32 Process::sys$exit(i32 status)
    {
        if (m_parent) {
            m_parent->m_terminated_children.enqueue({ m_process_id, status });

            ASSERT(m_parent->m_terminated_children.size() > 0);
        }

        FIXME();
        // Scheduler::the().terminate_active_thread();

        // Since we are in handler mode, we can't instantly terminate but instead
        // have to leave handler mode for PendSV to fire.
        //
        // Returning normally should clean up the stack and should be functionally
        // equivalent to doing the complex stuff required to terminate instantly.
        return -1;
    }

    i32 Process::sys$chdir(const char *pathname)
    {
        Path path { pathname };

        if (!path.is_absolute())
            path = m_working_directory / path;

        auto file_opt = Kernel::FileSystem::try_lookup(path);

        if (file_opt.is_error())
            return -file_opt.error();

        if ((file_opt.value()->m_mode & ModeFlags::Format) != ModeFlags::Directory)
            return -ENOTDIR;

        m_working_directory = path;

        return 0;
    }

    i32 Process::sys$posix_spawn(
        i32 *pid,
        const char *pathname,
        const UserlandSpawnFileActions *file_actions,
        const UserlandSpawnAttributes *attrp,
        char **argv,
        char **envp)
    {
        FIXME_ASSERT(file_actions == nullptr);
        FIXME_ASSERT(attrp == nullptr);

        dbgln("sys$posix_spawn(%p, %s, %p, %p, %p, %p)", pid, pathname, file_actions, attrp, argv, envp);

        Vector<String> arguments;
        while (*argv != nullptr)
            arguments.append(*argv++);

        Vector<String> environment;
        while (*envp != nullptr)
            arguments.append(*envp++);

        Path path { pathname };

        if (!path.is_absolute())
            path = m_working_directory / path;

        HashMap<String, String> system_to_host;
        system_to_host.set("/bin/Shell.elf", "Userland/Shell.1.elf");
        system_to_host.set("/bin/Example.elf", "Userland/Example.1.elf");
        system_to_host.set("/bin/Editor.elf", "Userland/Editor.1.elf");

        auto& file = dynamic_cast<FlashFile&>(FileSystem::lookup(path));
        ElfWrapper elf { file.m_data.data(), system_to_host.get_opt(path.string()).must() };

        auto& new_process = Kernel::Process::create(pathname, move(elf), arguments, environment);
        new_process.m_parent = this;
        new_process.m_working_directory = m_working_directory;

        dbgln("[Process::sys$posix_spawn] Created new process PID {} running {}", new_process.m_process_id, path);

        *pid = new_process.m_process_id;
        return 0;
    }

    i32 Process::sys$get_working_directory(u8 *buffer, usize *buffer_size)
    {
        auto string = m_working_directory.string();

        if (string.size() + 1 > *buffer_size) {
            *buffer_size = string.size() + 1;
            return -ERANGE;
        } else {
            string.strcpy_to({ (char*)buffer, *buffer_size });
            *buffer_size = string.size() + 1;
            return 0;
        }
    }
}
