// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Doubler: one or two extra "takes" of the part, made from short delays whose time drifts randomly (slightly off
// in time and pitch, like a second performance). In stereo they're panned out by Width; best after the amp and
// cab in Stereo mode. No config.
extern const BlockDescriptor kDoublerBlockDescriptor;

enum DoublerParam {
    kDoublerParamVoices, // choice: Single, Double
    kDoublerParamDelay,  // ms
    kDoublerParamDrift,  // %: how much timing and pitch wander
    kDoublerParamTone,   // Hz, low-pass on the doubles; off at the top of the range
    kDoublerParamWidth,  // %
    kDoublerParamMix,    // %
    kDoublerParamCount
};

} // namespace namsayin
