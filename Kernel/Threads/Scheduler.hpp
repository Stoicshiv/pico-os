#pragma once

#include <Std/Singleton.hpp>
#include <Std/CircularQueue.hpp>

#include <Kernel/Forward.hpp>
#include <Kernel/Threads/Thread.hpp>
#include <Kernel/SystemHandler.hpp>
#include <Kernel/PageAllocator.hpp>

namespace Kernel
{
    class Scheduler : public Singleton<Scheduler> {
    public:

    private:
        Thread m_default_thread;

        Thread *m_active_thread = nullptr;
        CircularQueue<Thread*, 16> m_queued_threads;

        friend Singleton<Scheduler>;
        Scheduler();
    };
}
