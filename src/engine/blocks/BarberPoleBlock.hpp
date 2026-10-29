// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Barber-pole phaser: a sweep that rises (or falls) forever. Three phasers sweep the same way, a third of a cycle
// apart, each fading in at the bottom of its sweep and out at the top, like the stripes on a barber's pole. Tempo
// sync (one sweep per note value). No config.
extern const BlockDescriptor kBarberPoleBlockDescriptor;

enum BarberPoleParam {
    kBarberParamRate,      // Hz, sweeps per second; when synced, a note value (rateNoteIndex)
    kBarberParamSync,      // toggle
    kBarberParamDirection, // choice: Up, Down
    kBarberParamStages,    // choice: 4, 6, 8 stages
    kBarberParamFeedback,  // %
    kBarberParamWidth,     // %
    kBarberParamMix,       // %
    kBarberParamCount
};

} // namespace xythos
