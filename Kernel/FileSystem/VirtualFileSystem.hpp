#pragma once

#include <Std/Map.hpp>
#include <Std/String.hpp>

#include <Kernel/Result.hpp>

namespace Kernel
{
    using namespace Std;

    class VirtualFile;
    class VirtualFileHandle;
    class VirtualDirectoryEntry;
    class VirtualFileSystem;

    enum class ModeFlags : u32 {
        Format    = 0b1111,
        Directory = 0b0001,
        Device    = 0b0010,
        Regular   = 0b0011,
    };

    inline ModeFlags operator&(ModeFlags lhs, ModeFlags rhs)
    {
        return static_cast<ModeFlags>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
    }

    class VirtualFileSystem {
    public:
        virtual VirtualDirectoryEntry& root() = 0;

        virtual VirtualFile& create_file() = 0;
        virtual VirtualDirectoryEntry& create_directory_entry() = 0;

        VirtualFile& create_regular();
    };

    class VirtualFile {
    public:
        u32 m_ino;
        ModeFlags m_mode;
        u32 m_size;
        u32 m_device;

        virtual VirtualFileSystem& filesystem() = 0;
        virtual VirtualFileHandle& create_handle() = 0;
    };

    class VirtualDirectoryEntry {
    public:
        VirtualDirectoryEntry()
        {
            m_entries.append(".", this);
        }

        virtual VirtualFile& file() = 0;

        void ensure_loaded()
        {
            if (!m_loaded)
                load();

            m_loaded = true;
        }

        void set_parent(VirtualDirectoryEntry& parent)
        {
            VERIFY(!m_entries.lookup("..").is_valid());
            m_entries.append("..", &parent);
        }

        Map<String, VirtualDirectoryEntry*> m_entries;
        bool m_loaded = false;

    protected:
        virtual void load() = 0;
    };

    class VirtualFileHandle {
    public:
        virtual VirtualFile& file() = 0;

        virtual KernelResult<usize> read(Bytes) = 0;
        virtual KernelResult<usize> write(ReadonlyBytes) = 0;
    };
}
