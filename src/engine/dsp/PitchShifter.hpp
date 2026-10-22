// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/DelayLine.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

// Delay-line pitch shifting. A voice reads the recent input through two taps that sweep a window at (1 - ratio)
// samples per sample, crossfaded with a Hann window so one is always fading in as the other jumps back.
// Polyphonic (chords shift as well as single notes), no detection and no reported latency; the costs are a slight
// warble and a trail of about half the window. Several voices can read one DelayLine (sized for the longest
// window plus a few samples).
class PitchVoice {
public:
    void setWindow(double sampleRate, double ms) { fWindow = std::max(64.0f, static_cast<float>(ms / 1000.0 * sampleRate)); }
    void reset(double phase = 0.0) { fPhase = phase; }

    // One output sample at `ratio` (2 = an octave up, 0.5 = an octave down); call after writing the input sample
    float read(const DelayLine& line, double ratio)
    {
        fPhase += (1.0 - ratio) / fWindow;
        fPhase -= std::floor(fPhase);
        float out = 0.0f;
        for (int tap = 0; tap < 2; ++tap)
        {
            double p = fPhase + 0.5 * tap;
            p -= std::floor(p);
            const float delay = kMinDelay + static_cast<float>(p) * fWindow;
            const float s = static_cast<float>(std::sin(M_PI * p));
            out += line.readFractional(delay) * s * s;
        }
        return out;
    }

    static constexpr float kMinDelay = 2.0f;

private:
    float fWindow = 2048.0f;
    double fPhase = 0.0;
};

inline double semitonesToRatio(double semitones)
{
    return std::exp2(semitones / 12.0);
}

} // namespace namsayin
