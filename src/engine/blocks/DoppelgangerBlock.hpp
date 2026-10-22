// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Doppelganger Delay: a delay whose echoes alternate between two effects. With the default order the 1st, 3rd,
// 5th... echoes are flanged and the 2nd, 4th, 6th... are chorused, each with only its own effect (they don't
// stack). Time, Sync, Feedback and Mix work like the Delay block's. No config.
extern const BlockDescriptor kDoppelgangerBlockDescriptor;

enum DoppelgangerParam {
    kDoppelParamTime,        // ms between echoes; when synced, a note value (delayNoteIndex), like the Delay block
    kDoppelParamSync,        // toggle
    kDoppelParamFeedback,    // %
    kDoppelParamMix,         // %
    kDoppelParamTone,        // Hz, low-pass on every repeat; off at the top of the range
    kDoppelParamOrder,       // choice: DoppelgangerOrder
    kDoppelParamWidth,       // % stereo offset of the two effects' LFOs between left and right
    kDoppelParamBounce,      // % flanged echoes lean left, chorused echoes right (stereo mode only)
    kDoppelParamFlangeRate,  // Hz
    kDoppelParamFlangeDepth, // %
    kDoppelParamFlangeRegen, // % flanger feedback (the "thick" in thick flanger)
    kDoppelParamFlangeMix,   // % of the flanger on its echoes; 0 = clean echoes
    kDoppelParamChorusRate,  // Hz
    kDoppelParamChorusDepth, // %
    kDoppelParamChorusVoices, // choice: 1, 2 or 3 voices
    kDoppelParamChorusMix,   // % of the chorus on its echoes; 0 = clean echoes
    kDoppelParamCount
};

enum class DoppelgangerOrder { FlangeFirst, ChorusFirst };

} // namespace namsayin
