// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Guitar synth: a monophonic oscillator that follows the played note, through a resonant low-pass whose cutoff
// rises with the playing (Env), with the guitar's own dynamics on the amplitude. The pitch comes from
// dsp/PitchTracker.hpp (YIN): a new note is found within about 4-24 ms and no latency is added; play single notes
// (chords confuse it, as with any guitar synth). The Pick Filter (dsp/PickFilter.hpp) keeps the pick attack's
// unsteady pitch from sounding. Waves: square, saw, pulse, and sub (saw plus a square an octave down). No config.
extern const BlockDescriptor kSynthBlockDescriptor;

enum SynthParam {
    kSynthParamWave,      // choice: SynthWave
    kSynthParamOctave,    // choice: -2, -1, 0, +1
    kSynthParamCutoff,    // Hz
    kSynthParamResonance, // Q
    kSynthParamEnv,       // %, how far the playing opens the filter (up to 5 octaves)
    kSynthParamAttack,    // ms
    kSynthParamGlide,     // ms
    kSynthParamMix,       // %
    kSynthParamPickFilter, // ms a new note must hold steady before it sounds (0 = off)
    kSynthParamCount
};

enum class SynthWave { Square, Saw, Pulse, Sub };

} // namespace namsayin
