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

// Bloom reverb: each new note feeds the reverb gradually, over Bloom, so the reverb swells up behind your playing
// instead of splashing on the pick attack. No config.
extern const BlockDescriptor kBloomReverbBlockDescriptor;

enum BloomReverbParam {
    kBloomParamDecay,  // s
    kBloomParamBloom,  // ms
    kBloomParamSize,   // %
    kBloomParamTone,   // Hz, damping
    kBloomParamLowCut, // Hz, on the wet signal
    kBloomParamMix,    // %
    kBloomParamCount
};

// Gated reverb (the 80s drum sound): a dense reverb that is cut off by a gate on your playing, Hold after the
// playing drops under Threshold, closing over Release. No config.
extern const BlockDescriptor kGatedReverbBlockDescriptor;

enum GatedReverbParam {
    kGatedRevParamSize,      // %
    kGatedRevParamDecay,     // s
    kGatedRevParamThreshold, // dB
    kGatedRevParamHold,      // ms
    kGatedRevParamRelease,   // ms
    kGatedRevParamTone,      // Hz, damping
    kGatedRevParamMix,       // %
    kGatedRevParamCount
};

} // namespace namsayin
