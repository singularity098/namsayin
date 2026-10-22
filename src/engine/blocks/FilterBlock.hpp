// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Filter: a resonant low-pass, band-pass, high-pass or notch, swept by an LFO (sine, triangle, saws, square, or
// random steps for a sample-and-hold "step filter"). Tempo sync. No config.
extern const BlockDescriptor kFilterBlockDescriptor;

enum FilterParam {
    kFilterParamType,      // choice: Low-pass, Band-pass, High-pass, Notch
    kFilterParamCutoff,    // Hz, the middle of the sweep
    kFilterParamResonance, // Q
    kFilterParamShape,     // choice: Sine, Triangle, Saw Up, Saw Down, Square, Steps
    kFilterParamRate,      // Hz; when synced, the same knob picks a note value (rateNoteIndex)
    kFilterParamSync,      // toggle
    kFilterParamDepth,     // %: 100% sweeps three octaves each way
    kFilterParamMix,       // %
    kFilterParamCount
};

// Robot filter (sample and hold): a resonant filter that jumps to a new cutoff on every clock tick, between Low and
// High. The new cutoff is random, from a random pattern that repeats every eight steps, or your playing level at the
// tick. Glide slides between the steps. Tempo sync locks the steps to the beat. No config.
extern const BlockDescriptor kRobotFilterBlockDescriptor;

enum RobotFilterParam {
    kRobotParamType,      // choice: Low-pass, Band-pass
    kRobotParamSource,    // choice: Random, Loop 8, Envelope
    kRobotParamLow,       // Hz
    kRobotParamHigh,      // Hz
    kRobotParamResonance, // Q
    kRobotParamRate,      // Hz, steps per second; when synced, a note value (rateNoteIndex)
    kRobotParamSync,      // toggle
    kRobotParamGlide,     // %, of a step
    kRobotParamMix,       // %
    kRobotParamCount
};

} // namespace namsayin
