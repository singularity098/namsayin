// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>
#include <cstdint>

namespace namsayin {

// Peak meter with a falling display and a held clip flag, so a UI polling at frame rate doesn't miss peaks
class Meter {
public:
    static constexpr float kClipLevel = 1.0f; // 0 dBFS

    void prepare(double sampleRate, float fallDbPerSecond = 24.0f, double clipHoldSeconds = 0.5)
    {
        fFall = std::pow(10.0f, -fallDbPerSecond / 20.0f / static_cast<float>(sampleRate));
        fClipHoldSamples = static_cast<uint32_t>(clipHoldSeconds * sampleRate);
        reset();
    }

    void process(const float* x, uint32_t frames)
    {
        float level = fLevel;
        bool clipped = false;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float a = std::fabs(x[i]);
            level = a > level ? a : level * fFall;
            clipped |= a >= kClipLevel;
        }
        fLevel = level;

        if (clipped)
            fClipCountdown = fClipHoldSamples;
        else
            fClipCountdown = fClipCountdown > frames ? fClipCountdown - frames : 0;
    }

    float level() const { return fLevel; }
    bool clipping() const { return fClipCountdown > 0; }

    void reset()
    {
        fLevel = 0.0f;
        fClipCountdown = 0;
    }

private:
    float fFall = 1.0f;
    float fLevel = 0.0f;
    uint32_t fClipHoldSamples = 0;
    uint32_t fClipCountdown = 0;
};

} // namespace namsayin
