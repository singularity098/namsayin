// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Tremolo in six types: sine, triangle and square volume tremolo; opto (a square through a photocell's fast-on,
// slow-off response, like a tube amp's tremolo); harmonic (lows and highs pulse in turns, like a brownface Fender);
// and pan (auto-pan in stereo, plain tremolo in mono). Tempo sync. No config.
extern const BlockDescriptor kTremoloBlockDescriptor;

enum TremoloParam {
    kTremParamRate,  // Hz; when synced, the same knob picks a note value (rateNoteIndex)
    kTremParamSync,  // toggle
    kTremParamDepth, // %
    kTremParamType,  // choice: TremoloType
    kTremParamLevel, // dB
    kTremParamCount
};

enum class TremoloType { Sine, Triangle, Square, Opto, Harmonic, Pan };

} // namespace xythos
