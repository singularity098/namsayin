// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Volume pedal: map an expression pedal to Volume with MIDI learn. The audio taper feels even across the travel;
// linear is there for swells that should come in late. No config.
extern const BlockDescriptor kVolumeBlockDescriptor;

enum VolumeParam {
    kVolumeParamVolume, // %
    kVolumeParamTaper,  // choice: Audio, Linear
    kVolumeParamCount
};

// Auto swell ("slow gear"): each new note fades in over Attack, like a volume-knob swell. A note is new when the
// playing comes up through Threshold after a pause, or (with Retrigger) on a fresh pick attack while still ringing.
// No config.
extern const BlockDescriptor kSwellBlockDescriptor;

enum SwellParam {
    kSwellParamAttack,    // ms
    kSwellParamThreshold, // dB
    kSwellParamRetrigger, // toggle
    kSwellParamCount
};

// The audio taper: 0..1 travel to gain
float volumeTaper(float travel);

} // namespace xythos
