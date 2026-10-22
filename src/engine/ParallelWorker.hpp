// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace namsayin {

// A thread that runs one job at a time for the audio thread: branch B of a split runs here while the audio thread
// runs branch A (docs/Architecture.md 4.4).
//
// start() and wait() are real-time safe: start() wakes the worker without locking, and wait() spins (then yields)
// until the job is done. The worker takes the audio thread's scheduling priority on its first job, where the OS
// allows it.
class ParallelWorker {
public:
    using Job = void (*)(void* context);

    ParallelWorker();
    ~ParallelWorker();

    ParallelWorker(const ParallelWorker&) = delete;
    ParallelWorker& operator=(const ParallelWorker&) = delete;

    // RT. Runs job(context) on the worker. Call wait() before the next start().
    void start(Job job, void* context);

    // RT. Returns once the job passed to start() has finished.
    void wait();

private:
    void run();
    void takeAudioThreadPriority();

    std::thread fThread;
    std::atomic<uint32_t> fRequested { 0 };
    std::atomic<uint32_t> fDone { 0 };
    std::atomic<bool> fStop { false };
    Job fJob = nullptr;
    void* fContext = nullptr;

    bool fPriorityRequested = false; // audio thread side
    std::atomic<bool> fTakePriority { false };
#ifndef _WIN32
    unsigned long fAudioThread = 0;  // pthread_t of the audio thread
#endif
};

} // namespace namsayin
