// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Guitar synth: a monophonic oscillator that follows the played note, through a resonant low-pass whose cutoff
// rises with the playing (Env), with the guitar's own dynamics on the amplitude. The pitch comes from the time
// between zero crossings of the filtered guitar, so it follows within about one cycle and adds no latency; play
// single notes (chords confuse it, as with any analog guitar synth). Waves: square, saw, pulse, and sub (saw plus a
// square an octave down). No config.
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
    kSynthParamCount
};

enum class SynthWave { Square, Saw, Pulse, Sub };

} // namespace namsayin
