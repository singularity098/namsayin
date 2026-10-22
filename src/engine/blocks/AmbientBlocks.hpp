// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Shimmer reverb: an FDN reverb whose tail is pitch shifted (an octave up by default) and fed back in, so it
// rises into a choir-like halo. No config.
extern const BlockDescriptor kShimmerBlockDescriptor;

enum ShimmerParam {
    kShimmerParamDecay,    // s
    kShimmerParamShimmer,  // %, how much shifted tail is fed back
    kShimmerParamInterval, // choice: +1 Oct, +5th, +Oct+5th, +2 Oct, -1 Oct
    kShimmerParamTone,     // Hz, damping
    kShimmerParamSize,     // %
    kShimmerParamLowCut,   // Hz, on the wet signal
    kShimmerParamMix,      // %
    kShimmerParamCount
};

// Freeze: while Freeze is on, the sound of the moment before holds indefinitely as a pad under your playing; off,
// it fades away. Layer keeps adding what you play into the held sound. The dry signal always passes. No config.
extern const BlockDescriptor kFreezeBlockDescriptor;

enum FreezeParam {
    kFreezeParamFreeze,  // toggle (map a footswitch to it)
    kFreezeParamLayer,   // toggle
    kFreezeParamFadeIn,  // ms
    kFreezeParamFadeOut, // ms
    kFreezeParamTone,    // Hz, low-pass on the held sound; off at the top of the range
    kFreezeParamLevel,   // %
    kFreezeParamCount
};

} // namespace namsayin
