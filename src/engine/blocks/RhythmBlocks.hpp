// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

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
// switch on and off with each press instead). Invert turns it around: silent until you press Kill. Rate Mod changes
// the stutter's speed as it runs (owner's request, 2026-10-23): Steps doubles it in steps (Time, Time/2, Time/4 ...,
// Speeds of them, each for Step Len cycles of Time, then over again), Sweep glides through the same speeds; Direction
// goes faster, slower, or back and forth. No config.
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
    kKillParamRateMod, // choice: Off, Steps, Sweep
    kKillParamSpeeds,  // choice: 2..5 speeds, each twice the last
    kKillParamStepLen, // choice: 1, 2, 4, 8 cycles of Time per speed
    kKillParamDirection, // choice: Faster, Slower, Both
    kKillParamCount
};

} // namespace xythos
