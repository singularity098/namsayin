// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// 7-band graphic EQ (100 Hz to 6.4 kHz in octaves, like a GE-7) with an output level. No config.
extern const BlockDescriptor kGraphicEqBlockDescriptor;

constexpr int kGraphicEqBands = 7;
constexpr float kGraphicEqFreqs[kGraphicEqBands] = { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };
constexpr int kGraphicEqParamLevel = kGraphicEqBands; // params 0..6 are the bands' gains in dB

// Parametric EQ: low and high cut, low and high shelves, one sweepable mid band, and an output level.
// The cuts are off at the ends of their ranges. No config.
extern const BlockDescriptor kParametricEqBlockDescriptor;

enum ParametricEqParam {
    kParEqParamLowCut,  // Hz
    kParEqParamLow,     // dB, shelf at 100 Hz
    kParEqParamMidFreq, // Hz
    kParEqParamMid,     // dB
    kParEqParamQ,
    kParEqParamHigh,    // dB, shelf at 4 kHz
    kParEqParamHighCut, // Hz
    kParEqParamLevel,   // dB
    kParEqParamCount
};

} // namespace xythos
