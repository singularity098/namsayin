// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Digital delay with a tone control in the feedback path, tempo sync, three stereo modes and modulation.
// No config.
extern const BlockDescriptor kDelayBlockDescriptor;

enum DelayParam {
    kDelayParamTime,     // ms; when synced, the same knob picks a note value (delayNoteIndex)
    kDelayParamSync,     // toggle
    kDelayParamFeedback, // %
    kDelayParamMix,      // %
    kDelayParamTone,     // Hz, low-pass in the feedback path; off at the top of the range
    kDelayParamMode,     // choice: DelayMode
    kDelayParamSpread,   // ms the right side is offset by (stereo and ping-pong)
    kDelayParamMod,      // % modulation depth
    kDelayParamCount
};

enum class DelayMode { Mono, Stereo, PingPong };

constexpr double kDelayMaxSeconds = 4.0;

// Tempo-synced note values, longest first
constexpr int kDelayNoteCount = 14;

// The note value a normalized Time knob position selects when synced
int delayNoteIndex(float normalizedTime);
const char* delayNoteName(int index);
double delayNoteBeats(int index); // length in quarter notes

} // namespace namsayin
