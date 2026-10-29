// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Wah pedal: Position is the pedal (map an expression pedal to it with MIDI learn). Types set the sweep range and
// resonance: Classic (a Cry Baby's), Vox, Wide and Bass. No config.
extern const BlockDescriptor kWahBlockDescriptor;

enum WahParam {
    kWahParamPosition,  // %, heel (0) to toe (100)
    kWahParamType,      // choice: WahType
    kWahParamResonance, // %, 50 is the type's own
    kWahParamLevel,     // dB
    kWahParamCount
};

enum class WahType { Classic, Vox, Wide, Bass };

// Auto wah / envelope filter: the filter follows the playing dynamics, sweeping up (or down) between Low and High.
// Filter types: Wah (the wah pedal's voicing), low-pass, band-pass and high-pass. No config.
extern const BlockDescriptor kAutoWahBlockDescriptor;

enum AutoWahParam {
    kAutoWahParamSensitivity, // dB of gain into the envelope
    kAutoWahParamDecay,       // ms
    kAutoWahParamLow,         // Hz
    kAutoWahParamHigh,        // Hz
    kAutoWahParamResonance,   // Q
    kAutoWahParamDirection,   // choice: Up, Down
    kAutoWahParamFilter,      // choice: AutoWahFilter
    kAutoWahParamMix,         // %
    kAutoWahParamCount
};

enum class AutoWahFilter { Wah, LowPass, BandPass, HighPass };

} // namespace xythos
