// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/DelayLine.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

// Delay-line pitch shifting. A voice reads the recent input through two taps whose delays move at (1 - ratio)
// samples per sample; when a tap reaches the end of its window it jumps back by the window, and the taps are
// crossfaded (Hann) so one is always fading in while the other jumps. Polyphonic (chords shift as well as single
// notes), no detection and no reported latency; the shifted sound trails by about half the window.
//
// Splices are clean only when the window is a whole number of the note's periods (the jump and the taps' offset
// then land in phase), so a block that knows the note passes pitchSyncWindow(); each tap takes up a new window when
// it jumps, so changing it never makes the delay jump mid-sweep. Several voices can read one DelayLine (sized for
// the longest window plus a few samples).
class PitchVoice {
public:
    static constexpr float kMinDelay = 2.0f;

    // `phase` 0..1 staggers voices that read one line
    void reset(float windowSamples, double phase = 0.0)
    {
        for (int k = 0; k < 2; ++k)
        {
            double p = phase + 0.5 * k;
            p -= std::floor(p);
            fTaps[k].window = windowSamples;
            fTaps[k].delay = kMinDelay + static_cast<float>(p) * windowSamples;
        }
    }

    // One output sample at `ratio` (2 = an octave up, 0.5 = an octave down); call after writing the input sample.
    // `window` (samples) is taken up by each tap at its next jump.
    float read(const DelayLine& line, double ratio, float window)
    {
        const auto step = static_cast<float>(1.0 - ratio);
        float out = 0.0f, weights = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            Tap& tap = fTaps[k];
            const Tap& other = fTaps[1 - k];
            tap.delay += step;
            if (tap.delay < kMinDelay || tap.delay > kMinDelay + tap.window)
            {
                // the end of the sweep: jump by the window, landing exactly half a window from the other tap, so
                // with a pitch-synchronous window the two stay in phase (falls back to a plain jump while the
                // other tap is still on an older window)
                const bool up = tap.delay < kMinDelay;
                const float jumped = up ? tap.delay + window : tap.delay - tap.window;
                const float locked = other.delay + (up ? 0.5f : -0.5f) * window;
                tap.window = window;
                tap.delay = std::fabs(locked - jumped) < 0.25f * window ? locked : jumped;
                tap.delay = std::clamp(tap.delay, kMinDelay, kMinDelay + tap.window);
            }
            const float p = (tap.delay - kMinDelay) / tap.window;
            const float s = std::sin(static_cast<float>(M_PI) * p);
            const float w = s * s;
            out += line.readFractional(tap.delay) * w;
            weights += w;
        }
        // the two weights sum to one while the taps share a window; keep the level when they don't
        return out / std::max(weights, 0.5f);
    }

private:
    struct Tap {
        float delay = kMinDelay;
        float window = 2048.0f;
    };
    Tap fTaps[2];
};

// A window near `baseSamples` that holds a whole, even number of periods of `hz` (even, so the taps' half-window
// offset is in phase too); `baseSamples` when the pitch isn't known
inline float pitchSyncWindow(float baseSamples, double hz, double sampleRate, float maxSamples)
{
    if (hz <= 0.0)
        return baseSamples;
    const double period = sampleRate / hz;
    const double periods = std::max(2.0, 2.0 * std::round(baseSamples / (2.0 * period)));
    double window = periods * period;
    while (window > maxSamples && window > 2.0 * period)
        window -= 2.0 * period;
    return static_cast<float>(std::min<double>(window, maxSamples));
}

inline double semitonesToRatio(double semitones)
{
    return std::exp2(semitones / 12.0);
}

} // namespace namsayin
