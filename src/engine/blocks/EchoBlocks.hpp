// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Tape echo (Space Echo style): three playback heads at 1x, 2x and 3x Time, in seven combinations; the tape
// saturates (Drive), loses highs and lows with each pass, and wavers in speed (Wow, which pitches the repeats).
// Time changes glide like a tape speed change. Tempo sync sets the first head's time. No config.
extern const BlockDescriptor kTapeEchoBlockDescriptor;

enum TapeEchoParam {
    kTapeParamTime,     // ms, first head; when synced, the same knob picks a note value (timeNoteIndex)
    kTapeParamSync,     // toggle
    kTapeParamFeedback, // %
    kTapeParamMix,      // %
    kTapeParamTone,     // Hz, low-pass on the repeats
    kTapeParamWow,      // %, wow and flutter
    kTapeParamDrive,    // %, tape saturation
    kTapeParamHeads,    // choice: tapeHeadsMask()
    kTapeParamCount
};

constexpr double kTapeMaxHeadMs = 600.0;

// Bit k set: head k+1 plays
int tapeHeadsMask(int choice);

// Ducking delay: the repeats duck under your playing and swell up in the gaps, so they don't crowd the notes.
// meter() is the ducking in dB. Tempo sync. No config.
extern const BlockDescriptor kDuckingDelayBlockDescriptor;

enum DuckingDelayParam {
    kDuckParamTime,      // ms; when synced, a note value (timeNoteIndex)
    kDuckParamSync,      // toggle
    kDuckParamFeedback,  // %
    kDuckParamMix,       // %
    kDuckParamTone,      // Hz, low-pass in the feedback path; off at the top of the range
    kDuckParamThreshold, // dB
    kDuckParamDuck,      // dB, the most the repeats are turned down
    kDuckParamRelease,   // ms
    kDuckParamCount
};

} // namespace namsayin
