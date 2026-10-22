// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// The pitch blocks share a delay-line pitch shifter (dsp/PitchShifter.hpp): polyphonic, zero reported latency,
// with a slight warble; a longer Window warbles less on low notes but trails more. No config.

// Pitch Shifter / harmonizer: two voices at fixed intervals (whole semitones) plus a shared detune, and the dry
// signal. One voice a few cents off is a detune chorus; +4 and +7 a major-chord harmony.
extern const BlockDescriptor kPitchShifterBlockDescriptor;

enum PitchShifterParam {
    kPitchParamShift1, // semitones, rounded
    kPitchParamLevel1, // %
    kPitchParamShift2, // semitones, rounded
    kPitchParamLevel2, // %
    kPitchParamDetune, // cents: voice 1 up, voice 2 down
    kPitchParamDry,    // %
    kPitchParamWindow, // ms
    kPitchParamTone,   // Hz, low-pass on the voices; off at the top of the range
    kPitchParamCount
};

// Whammy: Position (an expression pedal through MIDI learn) bends from no shift to the chosen interval. Whammy mode is
// the shifted signal only; Harmony adds the dry signal.
extern const BlockDescriptor kWhammyBlockDescriptor;

enum WhammyParam {
    kWhammyParamPosition, // %
    kWhammyParamInterval, // choice: whammyIntervalSemitones()
    kWhammyParamMode,     // choice: Whammy, Harmony
    kWhammyParamWindow,   // ms
    kWhammyParamCount
};

int whammyIntervalCount();
double whammyIntervalSemitones(int index);

// Poly Octave (POG style): octaves down and up, for chords too
extern const BlockDescriptor kPolyOctaveBlockDescriptor;

enum PolyOctaveParam {
    kPolyOctParamSub2, // %, two octaves down
    kPolyOctParamSub1, // %
    kPolyOctParamUp1,  // %
    kPolyOctParamUp2,  // %, two octaves up
    kPolyOctParamDry,  // %
    kPolyOctParamTone, // Hz, low-pass on the octaves; off at the top of the range
    kPolyOctParamWindow, // ms
    kPolyOctParamCount
};

// Harmonizer: one or two voices a scale interval away from the played note, in a key and scale, so a "third above"
// is major or minor as the scale needs. It follows single notes (dsp/PitchTracker.hpp) to know the note; the
// shifting itself works on anything. Notes outside the scale use their nearest scale note's interval.
extern const BlockDescriptor kHarmonizerBlockDescriptor;

enum HarmonizerParam {
    kHarmParamKey,    // choice: C .. B
    kHarmParamScale,  // choice: harmonizerScale()
    kHarmParamVoice1, // choice: scale steps, -Oct .. +Oct
    kHarmParamLevel1, // %
    kHarmParamVoice2, // choice
    kHarmParamLevel2, // %
    kHarmParamDry,    // %
    kHarmParamWindow, // ms
    kHarmParamCount
};

// The semitones from the played note (MIDI note number `note`) to the note `steps` scale steps away, for a key
// (0 = C) and scale (index into the Scale choice)
int harmonySemitones(int note, int key, int scale, int steps);

} // namespace namsayin
