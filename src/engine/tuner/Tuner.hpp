// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/tuner/PitchDetector.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace namsayin {

// The tuner's analysis side (docs/Architecture.md 4.9). The audio thread copies the dry input into a lock-free
// ring buffer with push(); analyze() runs on another thread and reads the latest audio from it.
//
// The pitch detector doesn't depend on the level, so a quiet input works as well as a loud one; only the gate
// (kGateDb, far below a guitar's level) decides whether there's anything to analyse.
class Tuner {
public:
    static constexpr double kWindowSeconds = 0.085; // analysis window; long enough for 31 Hz
    static constexpr double kMinFrequency = 25.0;
    static constexpr double kMaxFrequency = 1500.0;
    static constexpr double kGateDb = -74.0;        // RMS
    static constexpr double kMinClarity = 0.85;
    static constexpr double kSteadyRatio = 2.0;     // only audio whose level stays within this factor is analysed
    static constexpr double kChunkSeconds = 0.005;  // ...compared in chunks of whole periods at least this long

    // Non-RT, with audio stopped and no analyze() running
    void prepare(double sampleRate);

    // RT. Adds dry input; `right` may be null (mono). Stereo input is analysed as the sum of both channels.
    void push(const float* left, const float* right, uint32_t frames);

    // RT. Forgets the audio pushed so far (call when the tuner is switched on, so old audio isn't analysed).
    void restart() { fStart.store(fWritten.load(std::memory_order_relaxed), std::memory_order_relaxed); }

    // Non-RT. Analyses the most recent audio. Frequency is 0 when there's no clear note.
    PitchDetector::Result analyze();

private:
    PitchDetector fDetector;
    double fSampleRate = 48000.0;
    std::unique_ptr<std::atomic<float>[]> fRing; // power-of-two size
    uint32_t fRingMask = 0;
    std::atomic<uint64_t> fWritten { 0 }; // samples pushed in total
    std::atomic<uint64_t> fStart { 0 };   // first sample that may be analysed
    std::vector<float> fSpan;
};

} // namespace namsayin
