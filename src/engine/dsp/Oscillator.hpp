// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>

namespace xythos {

// Band-limited oscillator shapes for the synths. Phases run 0..1; dt is the phase step per sample (hz / rate).

// PolyBLEP: subtracts the step's aliasing around each discontinuity of a naive saw or square
inline double polyBlep(double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

inline float blepSaw(double phase, double dt)
{
    return static_cast<float>(2.0 * phase - 1.0 - polyBlep(phase, dt));
}

// A pulse that is high for `width` of the cycle
inline float blepPulse(double phase, double dt, double width)
{
    double v = phase < width ? 1.0 : -1.0;
    v += polyBlep(phase, dt);
    double falling = phase - width;
    if (falling < 0.0)
        falling += 1.0;
    v -= polyBlep(falling, dt);
    return static_cast<float>(v);
}

// Advances a phase by dt, wrapping at 1; returns true when it wrapped
inline bool advancePhase(double& phase, double dt)
{
    phase += dt;
    if (phase >= 1.0)
    {
        phase -= std::floor(phase);
        return true;
    }
    return false;
}

} // namespace xythos
