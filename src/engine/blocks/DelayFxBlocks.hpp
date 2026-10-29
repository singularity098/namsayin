// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Reverse delay: the repeats play backwards, each a Time-long slice of what you played, faded in and out so they
// swell. Tempo sync. No config.
extern const BlockDescriptor kReverseDelayBlockDescriptor;

enum ReverseDelayParam {
    kRevDelayParamTime,     // ms; when synced, a note value (timeNoteIndex)
    kRevDelayParamSync,     // toggle
    kRevDelayParamFeedback, // %
    kRevDelayParamMix,      // %
    kRevDelayParamTone,     // Hz, low-pass on the repeats; off at the top of the range
    kRevDelayParamCount
};

constexpr double kRevDelayMaxMs = 2000.0;

// Pitch delay: every repeat is shifted again, so the repeats climb (or fall) in steps, or drift with a few cents of
// Fine. Tempo sync. No config.
extern const BlockDescriptor kPitchDelayBlockDescriptor;

enum PitchDelayParam {
    kPitchDelayParamTime,     // ms; when synced, a note value (timeNoteIndex)
    kPitchDelayParamSync,     // toggle
    kPitchDelayParamFeedback, // %
    kPitchDelayParamMix,      // %
    kPitchDelayParamShift,    // semitones per repeat, rounded
    kPitchDelayParamFine,     // cents per repeat
    kPitchDelayParamTone,     // Hz, low-pass in the feedback path; off at the top of the range
    kPitchDelayParamCount
};

constexpr double kPitchDelayMaxMs = 2000.0;

// Beat repeat (glitch, stutter): while Repeat is held (or, with Latch, from one press to the next), the last Slice
// of what you played repeats over and over, instead of your playing. Gate shortens each repeat, Decay fades them,
// Pitch moves each repeat up or down, Reverse plays them backwards. Tempo sync. No config.
extern const BlockDescriptor kBeatRepeatBlockDescriptor;

enum BeatRepeatParam {
    kRepeatParamRepeat,  // momentary
    kRepeatParamLatch,   // toggle
    kRepeatParamSlice,   // ms; when synced, a note value (timeNoteIndex)
    kRepeatParamSync,    // toggle
    kRepeatParamGate,    // %
    kRepeatParamDecay,   // %, level lost per repeat
    kRepeatParamPitch,   // semitones per repeat, rounded
    kRepeatParamReverse, // toggle
    kRepeatParamMix,     // %
    kRepeatParamCount
};

constexpr double kRepeatMaxMs = 2000.0;

} // namespace xythos
