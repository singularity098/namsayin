// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Spring reverb: two springs, each a feedback loop with a long chain of stretched allpass filters that delays
// low frequencies more than high ones (dispersion), which gives the spring's "drip" and chirp. Based on Välimäki,
// Parker and Abel, "Parametric spring reverberation effect" (2010), simplified. Dwell drives the springs like an
// amp's reverb driver. No config.
extern const BlockDescriptor kSpringReverbBlockDescriptor;

enum SpringParam {
    kSpringParamDecay,   // s
    kSpringParamTension, // %: tighter springs are shorter and brighter
    kSpringParamDrip,    // %: how strongly the springs disperse (chirp)
    kSpringParamTone,    // Hz, damping
    kSpringParamDwell,   // %
    kSpringParamMix,     // %
    kSpringParamWidth,   // %
    kSpringParamCount
};

} // namespace namsayin
