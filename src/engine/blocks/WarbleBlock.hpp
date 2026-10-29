// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Warble (worn tape, warped vinyl): a slow, uneven pitch wobble (wow) with a faster flutter, a darker tone and a
// little saturation. Random makes the wobble drift instead of swing evenly. No config.
extern const BlockDescriptor kWarbleBlockDescriptor;

enum WarbleParam {
    kWarbleParamDepth,   // %, wow
    kWarbleParamRate,    // Hz, wow
    kWarbleParamFlutter, // %
    kWarbleParamRandom,  // %
    kWarbleParamTone,    // Hz, low-pass; off at the top of the range
    kWarbleParamDrive,   // %
    kWarbleParamMix,     // %
    kWarbleParamCount
};

} // namespace xythos
