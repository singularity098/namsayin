// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace xythos {

// Tap tempo: the average interval of the last few taps, starting over after a pause. Times are in seconds from any
// fixed point. No allocation, so the audio thread can use it (the looper's Tap footswitch) as well as the UI.
class TapTempo {
public:
    static constexpr double kTimeout = 2.0; // seconds between taps before it starts over
    static constexpr size_t kMaxTaps = 8;

    // A tap at `seconds`; returns the tempo in BPM from the taps so far, or 0 for the first tap
    double tap(double seconds)
    {
        if (fCount > 0 && (seconds - fTimes[fCount - 1] > kTimeout || seconds < fTimes[fCount - 1]))
            fCount = 0;
        if (fCount == kMaxTaps)
        {
            std::copy(fTimes.begin() + 1, fTimes.end(), fTimes.begin());
            --fCount;
        }
        fTimes[fCount++] = seconds;
        if (fCount < 2)
            return 0.0;
        const double average = (fTimes[fCount - 1] - fTimes[0]) / static_cast<double>(fCount - 1);
        return average > 0.0 ? 60.0 / average : 0.0;
    }

    void clear() { fCount = 0; }

private:
    std::array<double, kMaxTaps> fTimes {};
    size_t fCount = 0;
};

} // namespace xythos
