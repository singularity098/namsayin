// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/ParallelWorker.hpp"

#include <immintrin.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace namsayin {

ParallelWorker::ParallelWorker()
{
    fThread = std::thread([this] { run(); });
}

ParallelWorker::~ParallelWorker()
{
    fStop.store(true, std::memory_order_release);
    fRequested.fetch_add(1, std::memory_order_release);
    fRequested.notify_one();
    fThread.join();
}

void ParallelWorker::start(Job job, void* context)
{
    if (!fPriorityRequested)
    {
        fPriorityRequested = true;
#ifndef _WIN32
        fAudioThread = static_cast<unsigned long>(pthread_self());
#endif
        fTakePriority.store(true, std::memory_order_relaxed);
    }
    fJob = job;
    fContext = context;
    fRequested.fetch_add(1, std::memory_order_release);
    fRequested.notify_one();
}

void ParallelWorker::wait()
{
    const uint32_t target = fRequested.load(std::memory_order_relaxed);
    for (int spins = 0; fDone.load(std::memory_order_acquire) != target; ++spins)
    {
        if (spins < 4000)
            _mm_pause();
        else
            std::this_thread::yield();
    }
}

void ParallelWorker::run()
{
    // Flush denormals to zero, as hosts do on their audio threads
    _mm_setcsr(_mm_getcsr() | 0x8040);

    uint32_t seen = 0;
    for (;;)
    {
        fRequested.wait(seen, std::memory_order_acquire);
        seen = fRequested.load(std::memory_order_acquire);
        if (fStop.load(std::memory_order_acquire))
            return;
        if (fTakePriority.exchange(false, std::memory_order_acquire))
            takeAudioThreadPriority();
        fJob(fContext);
        fDone.store(seen, std::memory_order_release);
    }
}

void ParallelWorker::takeAudioThreadPriority()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    int policy = 0;
    sched_param param {};
    if (pthread_getschedparam(static_cast<pthread_t>(fAudioThread), &policy, &param) == 0)
        pthread_setschedparam(pthread_self(), policy, &param);
#endif
}

} // namespace namsayin
