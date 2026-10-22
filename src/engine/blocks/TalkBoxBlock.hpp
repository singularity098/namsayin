// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Talk box: three formant filters shape the guitar into vowels; Position moves through the chosen vowel path. It
// follows the Position knob (an expression pedal through MIDI learn), your picking (Envelope) or an LFO. No config.
extern const BlockDescriptor kTalkBoxBlockDescriptor;

enum TalkBoxParam {
    kTalkParamPosition,    // %
    kTalkParamVowels,      // choice: talkBoxPath()
    kTalkParamSource,      // choice: Pedal, Envelope, LFO
    kTalkParamSensitivity, // %, Envelope
    kTalkParamRate,        // Hz, LFO
    kTalkParamResonance,   // %
    kTalkParamMix,         // %
    kTalkParamLevel,       // dB
    kTalkParamCount
};

struct Formants {
    float f1, f2, f3; // Hz
};

// The formants at `position` (0..1) along vowel path `path` (Vowels choice)
Formants talkBoxFormants(int path, float position);

} // namespace namsayin
