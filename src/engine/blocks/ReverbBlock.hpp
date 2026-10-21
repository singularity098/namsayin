// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Algorithmic reverb: a Dattorro-style figure-eight tank, voiced as a room, a hall or a plate.
// In stereo the tail is fully stereo; in mono the two tank outputs are summed. No config.
extern const BlockDescriptor kReverbBlockDescriptor;

enum ReverbParam {
    kReverbParamType,     // choice: ReverbType
    kReverbParamDecay,    // seconds (RT60, roughly)
    kReverbParamPreDelay, // ms
    kReverbParamDamping,  // % high-frequency damping inside the tank
    kReverbParamLowCut,   // Hz, on the wet signal; off at the bottom of the range
    kReverbParamHighCut,  // Hz, on the wet signal; off at the top of the range
    kReverbParamWidth,    // % stereo width of the wet signal
    kReverbParamMix,      // %
    kReverbParamCount
};

enum class ReverbType { Room, Hall, Plate };

} // namespace namsayin
