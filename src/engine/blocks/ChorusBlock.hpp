// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Chorus in four types: Classic (one voice, like a CE-2), Dimension (two subtle voices in quadrature, wide),
// Tri (three voices 120 degrees apart, lush) and Vibrato (one voice, wet only: pitch vibrato). In stereo the
// voices' LFOs are offset between the sides by Width. No config.
extern const BlockDescriptor kChorusBlockDescriptor;

enum ChorusParam {
    kChorusParamRate,  // Hz
    kChorusParamDepth, // %
    kChorusParamMix,   // %
    kChorusParamType,  // choice: ChorusType
    kChorusParamTone,  // Hz, low-pass on the wet signal; off at the top of the range
    kChorusParamWidth, // %
    kChorusParamCount
};

enum class ChorusType { Classic, Dimension, Tri, Vibrato };

} // namespace xythos
