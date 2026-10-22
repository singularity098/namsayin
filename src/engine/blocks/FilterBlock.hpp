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

} // namespace namsayin
