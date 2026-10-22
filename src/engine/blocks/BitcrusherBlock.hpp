// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Bitcrusher / lo-fi: fewer bits (quantization grit) and a lower sample rate (aliasing, on purpose), then a
// low-pass to tame it. No config.
extern const BlockDescriptor kBitcrusherBlockDescriptor;

enum BitcrusherParam {
    kCrushParamBits, // 1..16 (fractional values give in-between step sizes)
    kCrushParamRate, // Hz, the reduced sample rate
    kCrushParamTone, // Hz, low-pass after; off at the top of the range
    kCrushParamMix,  // %
    kCrushParamCount
};

} // namespace namsayin
