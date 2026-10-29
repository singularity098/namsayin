// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>

namespace xythos {

// Clipping curves for the drive pedals: x / (1 + |x|^n)^(1/n). All have unit slope at 0 and saturate at +-1;
// a higher n gives a harder knee. Square roots only, no pow, since they run at 4x the sample rate.
namespace shaper {

// n = 2: a gentle knee, like diodes in an op-amp's feedback loop (Tube Screamer)
inline float soft(float x)
{
    return x / std::sqrt(1.0f + x * x);
}

// n = 4
inline float medium(float x)
{
    const float x2 = x * x;
    return x / std::sqrt(std::sqrt(1.0f + x2 * x2));
}

// n = 8: close to a hard clip with a rounded corner, like diodes to ground (RAT, DS-1)
inline float hard(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return x / std::sqrt(std::sqrt(std::sqrt(1.0f + x4 * x4)));
}

// Different knees for the two halves of the wave (germanium transistors, mismatched diodes): even harmonics
inline float asymmetric(float x, float positiveHeadroom, float negativeHeadroom)
{
    return x >= 0.0f ? positiveHeadroom * soft(x / positiveHeadroom) : negativeHeadroom * soft(x / negativeHeadroom);
}

} // namespace shaper

} // namespace xythos
