// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Phaser: a chain of swept first-order allpass stages mixed with the dry signal. Types: 4, 6, 8 and 12 stages,
// and Vibe (four stages spread far apart, swept by a lamp-like LFO, as in a Uni-Vibe; Mix at 100% gives its
// vibrato). Tempo sync. In stereo, the right side's LFO is offset by Width. No config.
extern const BlockDescriptor kPhaserBlockDescriptor;

enum PhaserParam {
    kPhaserParamRate,     // Hz; when synced, the same knob picks a note value (rateNoteIndex)
    kPhaserParamSync,     // toggle
    kPhaserParamDepth,    // %
    kPhaserParamCenter,   // Hz, the middle of the sweep
    kPhaserParamFeedback, // %, -90..90
    kPhaserParamMix,      // %
    kPhaserParamType,     // choice: PhaserType
    kPhaserParamWidth,    // %
    kPhaserParamCount
};

enum class PhaserType { Stages4, Stages6, Stages8, Stages12, Vibe };

} // namespace namsayin
