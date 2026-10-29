// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Flanger: a short swept delay with feedback (negative feedback gives the hollow sound, high feedback the jet).
// The sweep is exponential around the Manual delay, so the comb notches move evenly in pitch. Tempo sync. In
// stereo, the right side's LFO is offset by Width. No config.
extern const BlockDescriptor kFlangerBlockDescriptor;

enum FlangerParam {
    kFlangerParamRate,     // Hz; when synced, the same knob picks a note value (rateNoteIndex)
    kFlangerParamSync,     // toggle
    kFlangerParamDepth,    // %: 100% sweeps two octaves each way
    kFlangerParamManual,   // ms, the centre delay
    kFlangerParamFeedback, // %, -95..95
    kFlangerParamMix,      // %
    kFlangerParamWidth,    // %
    kFlangerParamCount
};

} // namespace xythos
