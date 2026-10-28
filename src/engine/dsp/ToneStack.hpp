// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace namsayin {

// Bass / Middle / Treble EQ: the global one at the end of the chain, and each NAM block's. At 0 dB on all three it is
// bypassed exactly.
class ToneStack {
public:
    static constexpr double kBassFreq = 120.0;
    static constexpr double kMiddleFreq = 800.0;
    static constexpr double kMiddleQ = 0.7;
    static constexpr double kTrebleFreq = 3200.0;

    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        for (Smoother& s : fGains)
        {
            // Coefficients are recomputed once per kUpdateInterval samples, so smooth at that rate
            s.prepare(sampleRate / kUpdateInterval, 0.03);
            s.snap(s.target());
        }
        updateCoefficients();
        reset();
    }

    void setGains(float bassDb, float middleDb, float trebleDb)
    {
        fGains[0].setTarget(bassDb);
        fGains[1].setTarget(middleDb);
        fGains[2].setTarget(trebleDb);
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames)
    {
        uint32_t done = 0;
        while (done < frames)
        {
            if (fCountdown == 0)
            {
                fCountdown = kUpdateInterval;
                if (!(fGains[0].settled() && fGains[1].settled() && fGains[2].settled()) || fDirty)
                {
                    for (Smoother& s : fGains)
                    {
                        s.next();
                        if (std::fabs(s.target() - s.value()) < 0.01f)
                            s.snap(s.target());
                    }
                    updateCoefficients();
                }
            }

            const uint32_t n = std::min(frames - done, fCountdown);
            if (!isFlat())
            {
                for (uint32_t ch = 0; ch < numChannels; ++ch)
                {
                    float* x = io[ch] + done;
                    for (uint32_t i = 0; i < n; ++i)
                        x[i] = fTreble[ch].process(fMiddle[ch].process(fBass[ch].process(x[i])));
                }
            }
            fCountdown -= n;
            done += n;
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fBass[ch].reset();
            fMiddle[ch].reset();
            fTreble[ch].reset();
        }
    }

private:
    static constexpr uint32_t kUpdateInterval = 32;

    bool isFlat() const
    {
        for (const Smoother& s : fGains)
        {
            if (s.value() != 0.0f || s.target() != 0.0f)
                return false;
        }
        return true;
    }

    void updateCoefficients()
    {
        fDirty = false;
        for (int ch = 0; ch < 2; ++ch)
        {
            fBass[ch].setLowShelf(fSampleRate, kBassFreq, fGains[0].value());
            fMiddle[ch].setPeak(fSampleRate, kMiddleFreq, fGains[1].value(), kMiddleQ);
            fTreble[ch].setHighShelf(fSampleRate, kTrebleFreq, fGains[2].value());
        }
    }

    double fSampleRate = 48000.0;
    Smoother fGains[3];
    Biquad fBass[2], fMiddle[2], fTreble[2];
    uint32_t fCountdown = 0;
    bool fDirty = true;
};

} // namespace namsayin
