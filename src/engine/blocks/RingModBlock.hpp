// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Ring modulator: the signal times a carrier (sine or square), giving sum and difference tones: bells, robots and
// clangs. An LFO can sweep the carrier's frequency. No config.
extern const BlockDescriptor kRingModBlockDescriptor;

enum RingModParam {
    kRingParamFrequency, // Hz
    kRingParamMix,       // %
    kRingParamRate,      // Hz, LFO
    kRingParamDepth,     // %: 100% sweeps an octave each way
    kRingParamWave,      // choice: Sine, Square
    kRingParamCount
};

} // namespace namsayin
