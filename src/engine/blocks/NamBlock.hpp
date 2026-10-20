// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// A local .nam model (A1 or A2).
//
// Config: {"path": "/abs/file.nam", "a2Size": 1.0}. a2Size is 1.0 for A2-Full and 0.0 for A2-Lite; it's config
// rather than a host parameter because switching it isn't real-time safe. Models that aren't slimmable ignore it.
//
// Not done yet: resampling when the host rate differs from the model's training rate (the model runs at the host
// rate, which changes its tone slightly). See docs/Architecture.md 4.5.
extern const BlockDescriptor kNamBlockDescriptor;

enum NamParam {
    kNamParamInput,     // dB
    kNamParamOutput,    // dB
    kNamParamNormalize, // toggle
    kNamParamCount
};

// Loudness that normalized models are brought to, in dB (the same target the NAM plugin uses)
constexpr double kNamTargetLoudnessDb = -18.0;

} // namespace namsayin
