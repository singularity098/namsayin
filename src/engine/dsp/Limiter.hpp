// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>
#include <cstdint>

namespace namsayin {

// Zero-latency safety limiter on the output: instant attack (the output never exceeds the ceiling),
// smooth release, stereo-linked.
class Limiter {
public:
    static constexpr float kCeiling = 0.891f; // -1 dBFS

    void prepare(double sampleRate, double releaseSeconds = 0.1)
    {
        fRelease = static_cast<float>(1.0 - std::exp(-1.0 / (releaseSeconds * sampleRate)));
        reset();
    }

    // Returns the lowest gain applied during this call (1 = no reduction)
    float process(float* const* io, uint32_t numChannels, uint32_t frames)
    {
        float minGain = 1.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            float peak = std::fabs(io[0][i]);
            if (numChannels > 1)
                peak = std::fmax(peak, std::fabs(io[1][i]));

            const float target = peak > kCeiling ? kCeiling / peak : 1.0f;
            if (target < fGain)
                fGain = target;
            else
                fGain += (target - fGain) * fRelease;

            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= fGain;
            minGain = std::fmin(minGain, fGain);
        }
        return minGain;
    }

    void reset() { fGain = 1.0f; }

private:
    float fRelease = 1.0f;
    float fGain = 1.0f;
};

} // namespace namsayin
