#include <Kernel/Process.hpp>
#include <Kernel/Scheduler.hpp>
#include <Kernel/DynamicLoader.hpp>
#include <Kernel/Interface/System.hpp>
#include <Kernel/FileSystem/FlashFileSystem.hpp>

namespace Kernel
{
    i32 Process::m_next_process_id = 0;

    Process& Process::active_process()
    {
        auto& thread = Scheduler::the().active_thread();
        return thread.m_process.must();
    }

    Process& Process::create(StringView name, ElfWrapper elf)
    {
        auto process = make<Process>(name);

        Thread thread { String::format("Process: {}", name), move(process) };

        return Scheduler::the().create_thread(move(thread), [name, elf] () mutable {
            dbgln("Loading executable for process '{}' from {}", name, elf.base());

            auto& process = Process::active_process();

            process.m_executable = load_executable_into_memory(elf);

            dbgln("Handing over execution to process '{}' at {}", name, process.m_executable.must().m_entry);

            hand_over_to_loaded_executable(process.m_executable.must());

            VERIFY_NOT_REACHED();
        }).m_process.must();
    }

    i32 Process::sys$read(i32 fd, u8 *buffer, usize count)
    {
        if (fd > 2)
            dbgln("[Process::sys$read] fd={} buffer={} count={}", fd, buffer, count);

        auto& handle = get_file_handle(fd);
        return handle.read({ buffer, count }).must();
    }

    i32 Process::sys$write(i32 fd, const u8 *buffer, usize count)
    {
        if (fd > 2)
            dbgln("[Process::sys$write] fd={} buffer={} count={}", fd, buffer, count);

        auto& handle = get_file_handle(fd);
        return handle.write({ buffer, count }).must();
    }

    i32 Process::sys$open(const char *pathname, u32 flags, u32 mode)
    {
        dbgln("[Process::sys$open] pathname={} flags={} mode={}", pathname, flags, mode);

        Path path = pathname;

        if (!path.is_absolute()) {
            dbgln("[Process::sys$open] m_working_directory={}", m_working_directory);
            path = m_working_directory / path;
        }

        auto& file = Kernel::FileSystem::lookup(path);

        if ((flags & O_DIRECTORY)) {
            if ((file.m_mode & ModeFlags::Format) != ModeFlags::Directory) {
                dbgln("[Process::sys$open] -> ENOTDIR");
                return -ENOTDIR;
            }
        }

        auto& handle = file.create_handle();
        return add_file_handle(handle);
    }

    i32 Process::sys$close(i32 fd)
    {
        dbgln("[Process::sys$close] fd={}", fd);

        // FIXME

        return 0;
    }

    i32 Process::sys$fstat(i32 fd, UserlandFileInfo *statbuf)
    {
        dbgln("[Process::sys$fstat] fd={} statbuf={}", fd, statbuf);

        auto& handle = get_file_handle(fd);
        auto& file = handle.file();

        statbuf->st_dev = FileSystemId::Invalid;
        statbuf->st_rdev = 0xdead;
        statbuf->st_size = 0xdead;
        statbuf->st_blksize = 0xdead;
        statbuf->st_blocks = 0xdead;

        statbuf->st_ino = file.m_ino;
        statbuf->st_mode = file.m_mode;
        statbuf->st_uid = file.m_owning_user;
        statbuf->st_gid = file.m_owning_group;

        return 0;
    }

    i32 Process::sys$fork()
    {
        auto new_process = make<Process>(
            String::format("Fork: {}", m_name),
            m_executable.must().clone()
        );

        new_process->m_parent = this;

        i32 new_process_id = new_process->m_process_id;
        dbgln("[Process::sys$fork] Forking new process PID {} from PID {}", new_process_id, m_process_id);

        auto& thread = Scheduler::the().active_thread();
        auto& executable = m_executable.must();
        auto& new_executable = new_process->m_executable.must();

        auto *new_context = reinterpret_cast<FullRegisterContext*>(new_executable.m_stack_base + (reinterpret_cast<u8*>(thread.m_context.must()) - executable.m_stack_base));
        new_context->r0.m_storage = 0;
        new_context->r9.m_storage = new_executable.m_writable_base;

        Thread new_thread {
            String::format("Process: {}", new_process->m_name),
            move(new_process),
            new_context,
        };

        dbgln("Creating new thread '{}'", new_thread.m_name);

        Scheduler::the().create_thread(move(new_thread));

        dbgln("Returning to parent process with PID {}", new_process_id);

        return new_process_id;
    }

    i32 Process::sys$wait(i32 *status)
    {
        dbgln("[PID {} ({})] Checking wait condition...", m_process_id, this);
        dbgln("  m_terminated_children.size() = {}", m_terminated_children.size());

        if (m_terminated_children.size() > 0) {
            auto terminated_child_process = m_terminated_children.dequeue();
            *status = terminated_child_process.m_status;

            dbgln("[PID {}] Detected terminated child {}", m_process_id, terminated_child_process.m_process_id);
            return terminated_child_process.m_process_id;
        }

        Scheduler::the().donate_my_remaining_cpu_slice();
        return -EINTR;
    }

    i32 Process::sys$execve(const char *pathname, char **argv, char **envp)
    {
        Path path = pathname;

        if (!path.is_absolute())
            path = m_working_directory / path;

        // FIXME: Deal with 'argv' and 'envp'

        // FIXME: We blindly assume that this file is in the flash
        auto& file = dynamic_cast<FlashFile&>(FileSystem::lookup(path));

        // FIXME: Do this properly
        StringView fullpath;
        if (StringView { pathname } == "Example.elf" || StringView { pathname } == "/bin/Example.elf") {
            fullpath = "Userland/Example.1.elf";
        } else if (StringView { pathname } == "Shell.elf") {
            fullpath = "Userland/Shell.1.elf";
        } else {
            VERIFY_NOT_REACHED();
        }

        ElfWrapper elf { file.m_data.data(), fullpath };

        auto& process = Process::active_process();

        process.m_name = String::format("Exec: '{}'", argv[0]);
        process.m_executable = load_executable_into_memory(elf);

        hand_over_to_loaded_executable(process.m_executable.must());

        VERIFY_NOT_REACHED();
    }

    i32 Process::sys$exit(i32 status)
    {
        if (m_parent) {
            dbgln("[PID {}] Informing parent [PID {} ({})] about termination", m_process_id, m_parent->m_process_id, m_parent);
            m_parent->m_terminated_children.enqueue({ m_process_id, status });

            ASSERT(m_parent->m_terminated_children.size() > 0);
        }

        Scheduler::the().terminate_active_thread();

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

        m_working_directory = path;

        return 0;
    }
}
