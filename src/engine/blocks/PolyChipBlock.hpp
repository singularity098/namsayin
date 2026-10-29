// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Poly Chip: a polyphonic chip synth. Instead of tracking one pitch, it splits the guitar into one narrow band per
// semitone (B0..E6) and plays each note it finds as an 8-bit voice at that semitone, so chords work. Peaks across
// the bands are the notes; Focus drops peaks that are the harmonics of a lower note; Voices keeps the loudest few.
// Arp plays the held notes one after another instead of together. Quantize off plays each note at the pitch measured
// in its band (bends, vibrato). Pick Filter makes a new note wait a few ms, so the pick attack doesn't sound. Low
// notes take longer to speak than in the mono synths (the narrow bands ring up: half level after about 18 ms on the
// low E string, 6-8 ms mid-neck). Track picks the block's input or the rig's. Config: "lookahead" (ms, 0 by default,
// in whole 0.67 ms hops): the voices and the dry signal play that much behind, and each note starts back where it
// began, so up to the lookahead, notes start on time.
extern const BlockDescriptor kPolyChipBlockDescriptor;

enum PolyChipParam {
    kPolyChipParamWave,    // choice: Pulse 12%, Pulse 25%, Pulse 50%, Triangle
    kPolyChipParamVoices,  // choice: 1..6
    kPolyChipParamFocus,   // %, how hard harmonics of lower notes are dropped
    kPolyChipParamArp,     // choice: Off, Up, Down
    kPolyChipParamArpRate, // Hz, notes per second
    kPolyChipParamRelease, // ms
    kPolyChipParamCrush,   // %
    kPolyChipParamOctave,  // choice: -2, -1, 0, +1
    kPolyChipParamMix,     // %
    // added after release: at the end, so saved values keep their meaning
    kPolyChipParamQuantize,   // toggle: snap to semitones; off follows bends and vibrato
    kPolyChipParamPickFilter, // ms a new note must last before it sounds (filters the pick attack)
    kPolyChipParamTrack,      // choice: Block In, Rig In: what the notes are found from
    kPolyChipParamCount
};

enum class PolyChipArp { Off, Up, Down };

} // namespace namsayin
