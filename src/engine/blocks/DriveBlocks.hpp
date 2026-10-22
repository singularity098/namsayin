// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// The drive pedals. Each runs its clipping at 4x the sample rate (zero-latency IIR oversampling) to keep aliasing
// down; filters around it run at the normal rate. No config.

// Overdrive: Screamer (mid hump, clipping added on top of the clean signal), Blues (flatter, more open) and
// Transparent (a clean blend that gives way to a mid-boosted drive as Drive goes up).
extern const BlockDescriptor kOverdriveBlockDescriptor;

enum OverdriveParam {
    kOdParamDrive, // %
    kOdParamTone,  // %
    kOdParamLevel, // dB
    kOdParamType,  // choice: OverdriveType
    kOdParamMix,   // %
    kOdParamCount
};

enum class OverdriveType { Screamer, Blues, Transparent };

// Distortion: RAT (op-amp gain that loses highs as it goes up, hard clipping, a Filter that sweeps down), DS-1
// (asymmetric clipping, scooped tone) and Metal (two gain stages, tight lows, scooped mids).
extern const BlockDescriptor kDistortionBlockDescriptor;

enum DistortionParam {
    kDistParamDrive, // %
    kDistParamTone,  // %
    kDistParamLevel, // dB
    kDistParamType,  // choice: DistortionType
    kDistParamCount
};

enum class DistortionType { Rat, Ds1, Metal };

// Fuzz: Face (germanium, asymmetric; Bias below 50% starves it into a gated sputter), Muff (two cascaded
// clipping stages, scooped tone) and Octave (full-wave rectified: an octave up, like an Octavia).
extern const BlockDescriptor kFuzzBlockDescriptor;

enum FuzzParam {
    kFuzzParamFuzz,  // %
    kFuzzParamTone,  // %
    kFuzzParamLevel, // dB
    kFuzzParamType,  // choice: FuzzType
    kFuzzParamBias,  // %, 50 is normal
    kFuzzParamCount
};

enum class FuzzType { Face, Muff, Octave };

// Boost: Clean (flat, with a pedal's soft headroom), Treble (a Rangemaster-style treble booster) and Mid.
extern const BlockDescriptor kBoostBlockDescriptor;

enum BoostParam {
    kBoostParamGain, // dB
    kBoostParamType, // choice: BoostType
    kBoostParamTone, // %, 50 is flat
    kBoostParamCount
};

enum class BoostType { Clean, Treble, Mid };

} // namespace namsayin
