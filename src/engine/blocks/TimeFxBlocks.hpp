// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Tape Stop: hold Stop and the sound slows to a halt like a turntable losing power (pitch and speed dropping
// together over Stop Time); let go and it spins back up over Start Time and lands back in time with your playing.
// The playback reads a buffer at the falling speed; for the spin-up it jumps back by exactly the distance the
// rising speed will make up, so it arrives at the live signal as it reaches full speed. Latch makes Stop switch on
// and off with each press. No config.
extern const BlockDescriptor kTapeStopBlockDescriptor;

enum TapeStopParam {
    kTapeStopParamStop,      // momentary
    kTapeStopParamLatch,     // toggle
    kTapeStopParamStopTime,  // ms
    kTapeStopParamStartTime, // ms (0: straight back)
    kTapeStopParamCurve,     // %, 0 = a steady slope, 100 = falling fast, then a long slow tail
    kTapeStopParamCount
};

// Bouncing Ball Delay: echoes that speed up (Fall) or slow down (Rise) like a ball bouncing to rest: each gap is
// Gravity shorter than the last, each bounce Bounce times as loud and a little darker (Tone), alternating across the
// stereo field (Spread). Time is the first (Fall) or last (Rise) gap. No config.
extern const BlockDescriptor kBounceDelayBlockDescriptor;

enum BounceDelayParam {
    kBounceParamTime,      // ms; when synced, a note value (timeNoteIndex)
    kBounceParamSync,      // toggle
    kBounceParamGravity,   // %
    kBounceParamDirection, // choice: Fall, Rise
    kBounceParamBounces,   // choice: 4, 6, 8, 12, 16, 24
    kBounceParamBounce,    // %, each bounce's level relative to the last
    kBounceParamTone,      // Hz, the first bounce's low-pass (later ones darker)
    kBounceParamSpread,    // %
    kBounceParamMix,       // %
    kBounceParamCount
};

// Pattern Delay: eight echoes on a grid of Time (a 16th note at 120 BPM by default), each at its own level, so the
// echoes play a rhythm; Feedback repeats the whole pattern. Pitch sends every other echo through a pitch shifter,
// Spread alternates them left and right, Tone darkens them. No config.
extern const BlockDescriptor kPatternDelayBlockDescriptor;

enum PatternDelayParam {
    kPatternParamTap1, // %, the echo one step after the note
    kPatternParamTap2,
    kPatternParamTap3,
    kPatternParamTap4,
    kPatternParamTap5,
    kPatternParamTap6,
    kPatternParamTap7,
    kPatternParamTap8,
    kPatternParamTime,     // ms, one step; when synced, a note value
    kPatternParamSync,     // toggle
    kPatternParamFeedback, // %, the pattern again after its eighth step
    kPatternParamPitch,    // choice: Off, Oct Up, Oct Down, Fifth, Up + Down
    kPatternParamSpread,   // %
    kPatternParamTone,     // Hz
    kPatternParamMix,      // %
    kPatternParamCount
};
constexpr int kPatternTaps = 8;

} // namespace xythos
