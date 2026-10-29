// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Modulation Sweeper: two or three modulation effects (chorus, flanger, phaser, vibrato) that it moves between
// over time. Every Sweep Rate step it glides to the next effect in the pattern (Back & Forth: 1 2 3 2 1...,
// Cycle: 1 2 3 1...); Glide sets how much of each step is the crossfade (0%: a quick switch, 100%: always
// morphing). The sweep can sync to the host tempo, one step per note value. All four effects run all the time,
// so a crossfade never starts from a cold delay line. No config.
extern const BlockDescriptor kModSweeperBlockDescriptor;

enum ModSweeperParam {
    kSweeperParamEffect1,      // choice: SweeperEffect
    kSweeperParamEffect2,      // choice: SweeperEffect
    kSweeperParamEffect3,      // choice: None, then SweeperEffect + 1
    kSweeperParamPattern,      // choice: SweeperPattern
    kSweeperParamSweepRate,    // Hz, steps per second; when synced, a note value per step (rateNoteIndex)
    kSweeperParamSync,         // toggle
    kSweeperParamGlide,        // % of each step spent crossfading
    kSweeperParamDepth,        // % for every effect
    kSweeperParamMix,          // % for chorus, flanger and phaser (vibrato is wet only)
    kSweeperParamFeedback,     // %, -90..90, flanger and phaser
    kSweeperParamWidth,        // % stereo LFO offset
    kSweeperParamChorusRate,   // Hz
    kSweeperParamFlangerRate,  // Hz
    kSweeperParamPhaserRate,   // Hz
    kSweeperParamVibratoRate,  // Hz
    kSweeperParamCount
};

enum class SweeperEffect { Chorus, Flanger, Phaser, Vibrato };
enum class SweeperPattern { BackAndForth, Cycle };

} // namespace xythos
