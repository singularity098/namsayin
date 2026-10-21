// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>
#include <vector>

namespace namsayin {

// Fixed delay of up to two channels, used to time-align the branches of a split
class DelayLine {
public:
    // Non-RT
    void prepare(uint32_t delay)
    {
        fDelay = delay;
        for (std::vector<float>& buffer : fBuffer)
            buffer.assign(delay, 0.0f);
        fPos = 0;
    }

    uint32_t delay() const { return fDelay; }

    // RT. In place.
    void process(float* const* io, uint32_t numChannels, uint32_t frames)
    {
        if (fDelay == 0)
            return;
        uint32_t pos = fPos;
        for (uint32_t ch = 0; ch < numChannels; ++ch)
        {
            float* buffer = fBuffer[ch].data();
            pos = fPos;
            for (uint32_t i = 0; i < frames; ++i)
            {
                const float out = buffer[pos];
                buffer[pos] = io[ch][i];
                io[ch][i] = out;
                if (++pos == fDelay)
                    pos = 0;
            }
        }
        fPos = pos;
    }

private:
    std::vector<float> fBuffer[2];
    uint32_t fDelay = 0;
    uint32_t fPos = 0;
};

} // namespace namsayin
