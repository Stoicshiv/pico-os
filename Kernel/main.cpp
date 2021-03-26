#include <pico/sync.h>
#include <hardware/uart.h>
#include <hardware/gpio.h>

#include <Std/Forward.hpp>
#include <Std/Format.hpp>
#include <Kernel/DynamicLoader.hpp>
#include <Kernel/ConsoleDevice.hpp>
#include <Kernel/FileSystem/MemoryFileSystem.hpp>
#include <Kernel/FileSystem/FlashFileSystem.hpp>
#include <Kernel/Process.hpp>
#include <Kernel/MemoryAllocator.hpp>

#include <pico/stdio.h>

extern "C" {
    extern u8 __fs_start[];
    extern u8 __fs_end[];
}

void load_and_execute_shell()
{
    auto& shell_dentry_info = Kernel::MemoryFileSystem::the().lookup_path("/bin/Shell.elf");

    ElfWrapper elf { shell_dentry_info.m_info->m_direct_blocks[0] };
    LoadedExecutable executable = load_executable_into_memory(elf);

    // FIXME: Do this properly
    Kernel::Process::current();

    dbgln("Switching to shell process");

    asm volatile(
        "movs r0, #0;"
        "msr psp, r0;"
        "isb;"
        "movs r0, #0b11;"
        "msr control, r0;"
        "isb;"
        "mov r0, %1;"
        "mov sb, %2;"
        "blx %0;"
        :
        : "r"(executable.m_entry), "r"(executable.m_stack_base + executable.m_stack_size), "r"(executable.m_writable_base)
        : "r0");

    VERIFY_NOT_REACHED();
}

void initialize_uart_debug()
{
    // FIXME: I really don't know that the SDK does exactly, but this is necessary to make assertions work.
    stdio_init_all();

    // FIXME: For some reason there is a 0xff symbol send when the connection is opened.
    char ch = uart_getc(uart0);
    VERIFY(ch == 0xff);
}

struct X {
    X(int i)
        : m_i(i)
    {
        dbgln("X(%)", m_i);
    }
    ~X()
    {
        dbgln("~X() with x = { % }", m_i);
    }
    X(X&& x)
    {
        dbgln("X(X&& x) with x = { % }", x.m_i);
        m_i = x.m_i;
        x.m_i = 0;
    }
    X(const X& x)
    {
        dbgln("X(const X& x) with x = { % }", x.m_i);
        m_i = x.m_i;
    }

    X& operator=(const X& x)
    {
        dbgln("operator=(const X& x) with x = { % }", x.m_i);
        m_i = x.m_i;
        return *this;
    }
    X& operator=(X&& x)
    {
        dbgln("operator=(X&& x) with x = { % }", x.m_i);
        m_i = x.m_i;
        x.m_i = 0;
        return *this;
    }

    int m_i;
};

int main()
{
    initialize_uart_debug();
    dbgln("\e[0;1mBOOT\e[0m");

    Kernel::MemoryAllocator::the();
    Kernel::MemoryFileSystem::the();
    Kernel::FlashFileSystem::the();
    Kernel::ConsoleDevice::the();

    // FIXME: I got the abstractions wrong somehow
    dbgln("[main] Creating /example.txt");
    auto& example_info = Kernel::MemoryFileSystem::the().create_regular();
    auto& example_dentry = Kernel::MemoryFileSystem::the().root().add_entry("example.txt", example_info);
    auto& example_file = Kernel::FileSystem::file_from_info(example_info);
    auto& example_handle = example_file.create_handle();
    example_handle.write({ (const u8*)"Hello, world!\n", 14 });

    load_and_execute_shell();

    for(;;)
        __wfi();
}
