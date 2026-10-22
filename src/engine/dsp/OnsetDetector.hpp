// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Svf.hpp"

#include <cmath>

namespace namsayin {

// Pick-attack detection for blocks that react to each new note (organ attack, sustainer, bloom reverb): a fast
// envelope jumping to twice a slower one, over a threshold, at most once per 60 ms. A note after silence always
// counts; a fresh pick while the last note still rings counts when it is clearly louder than the ringing.
class OnsetDetector {
public:
    static constexpr float kJump = 2.0f;

    // Non-RT
    void prepare(double sampleRate)
    {
        fFast.prepare(sampleRate, 0.5, 30.0);
        fSlow.prepare(sampleRate, 20.0, 300.0);
        fRefractory = static_cast<int>(0.06 * sampleRate);
        reset();
    }

    void setThreshold(float gain) { fThreshold = gain; }

    void reset()
    {
        fFast.reset();
        fSlow.reset();
        fWait = 0;
    }

    // One sample (the rectified level of the input); true on a new note
    bool process(float level)
    {
        const float slowBefore = fSlow.value();
        const float fast = fFast.process(level);
        fSlow.process(level);
        if (fWait > 0)
        {
            --fWait;
            return false;
        }
        if (fast > fThreshold && fast > kJump * slowBefore)
        {
            fWait = fRefractory;
            return true;
        }
        return false;
    }

    // The fast envelope: the playing level now
    float level() const { return fFast.value(); }

private:
    EnvelopeFollower fFast, fSlow;
    float fThreshold = 0.003f; // about -50 dB
    int fRefractory = 2880;
    int fWait = 0;
};

} // namespace namsayin
