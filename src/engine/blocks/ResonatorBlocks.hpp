// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Sympathetic Strings: twelve strings tuned to a key and a tuning, which ring along when you play their notes (or
// notes whose harmonics they share), like the drone strings of a sitar or the strings of a harp left undamped. Each
// string is a comb resonator (a delay line of one period, fed back through a damping low-pass); the guitar's signal
// excites them all, so only matching notes build up. The strings alternate left and right (Width). No config.
extern const BlockDescriptor kSympatheticBlockDescriptor;

enum SympatheticParam {
    kSympParamKey,     // choice: C .. B
    kSympParamTuning,  // choice: Drone, Major, Minor, Sitar, Open
    kSympParamDecay,   // s, how long the strings ring (T60 at the strings' notes)
    kSympParamDamping, // %, how fast their highs die
    kSympParamExcite,  // %, how strongly your playing sets them ringing
    kSympParamWidth,   // %
    kSympParamLevel,   // %, of the strings (the dry signal stays at full level)
    kSympParamCount
};

// Jawari: the buzzing bridge of a sitar. The string grazes a curved bridge, folding the top of each swing back (at
// 4x oversampling); Bloom makes the buzz grow as the note fades, as the contact point moves on a real bridge, and
// Color sets the resonance the buzz rings through. No config.
extern const BlockDescriptor kJawariBlockDescriptor;

enum JawariParam {
    kJawariParamBuzz,  // %
    kJawariParamBloom, // %
    kJawariParamColor, // Hz, the buzz's resonance
    kJawariParamMix,   // %
    kJawariParamLevel, // dB
    kJawariParamCount
};

} // namespace xythos
