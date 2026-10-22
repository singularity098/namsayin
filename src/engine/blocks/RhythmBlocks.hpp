// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Slicer: a step sequencer for volume. Each step plays at its own level, one step per Time; Steps sets how many of
// the eight are used before the pattern starts again. Tempo sync locks the steps to the beat. No config.
extern const BlockDescriptor kSlicerBlockDescriptor;

constexpr int kSlicerSteps = 8;

enum SlicerParam {
    kSlicerParamStep1, // % (steps 1..8 follow)
    kSlicerParamSteps = kSlicerParamStep1 + kSlicerSteps, // choice: 2..8
    kSlicerParamTime,   // ms per step; when synced, a note value (timeNoteIndex)
    kSlicerParamSync,   // toggle
    kSlicerParamSmooth, // %, how softly the steps change
    kSlicerParamDepth,  // %
    kSlicerParamCount
};

// Kill switch: Kill mutes while held, and Stutter chops the sound on and off at Time while held (Latch makes both
// switch on and off with each press instead). Invert turns it around: silent until you press Kill. No config.
extern const BlockDescriptor kKillSwitchBlockDescriptor;

enum KillSwitchParam {
    kKillParamKill,    // momentary
    kKillParamStutter, // momentary
    kKillParamLatch,   // toggle
    kKillParamInvert,  // toggle
    kKillParamTime,    // ms, one on-off cycle of the stutter; when synced, a note value (timeNoteIndex)
    kKillParamSync,    // toggle
    kKillParamDuty,    // %, how much of each cycle sounds
    kKillParamFade,    // ms
    kKillParamCount
};

} // namespace namsayin
