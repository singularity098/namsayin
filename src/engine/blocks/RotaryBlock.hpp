// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Rotary speaker (Leslie): the highs go to a spinning horn, the lows to a slower drum, each with its Doppler pitch
// shift and level swing; two virtual microphones Width apart make the stereo image. Switching Speed makes the
// horn and drum speed up or slow down at their own rates, scaled by Ramp. Drive overdrives the amp in front.
// No config.
extern const BlockDescriptor kRotaryBlockDescriptor;

enum RotaryParam {
    kRotaryParamSpeed,   // choice: Slow, Fast (map a footswitch to it)
    kRotaryParamSlow,    // Hz, horn rotation at Slow
    kRotaryParamFast,    // Hz, horn rotation at Fast
    kRotaryParamRamp,    // %, how long speed changes take (100% is the real thing's few seconds)
    kRotaryParamDrive,   // %
    kRotaryParamBalance, // dB, horn against drum
    kRotaryParamWidth,   // %
    kRotaryParamMix,     // %
    kRotaryParamCount
};

} // namespace namsayin
