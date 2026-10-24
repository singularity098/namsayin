// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Chaos Mod: a Lorenz attractor (the "butterfly" of chaos theory) moves a resonant low-pass (Filter, around
// Cutoff), the volume (Volume) and the pitch (Pitch, a vibrato from a moving delay). The motion is smooth but never
// repeats: somewhere between an LFO and a random sample-and-hold. Chaos sets how wild it is (the attractor's rho, 25
// to 45), Speed how fast it runs; the right channel follows a second attractor, started a hair apart, which drifts
// away from the first (Width). No config.
extern const BlockDescriptor kChaosModBlockDescriptor;

enum ChaosModParam {
    kChaosParamSpeed,     // Hz-ish: attractor time per second
    kChaosParamChaos,     // %
    kChaosParamFilter,    // %, how far it sweeps the filter (up to 3 octaves each way)
    kChaosParamCutoff,    // Hz
    kChaosParamResonance, // Q
    kChaosParamVolume,    // %
    kChaosParamPitch,     // %
    kChaosParamWidth,     // %
    kChaosParamMix,       // %
    kChaosParamCount
};

// Acoustic Simulator: makes an electric guitar sound like an acoustic one. It undoes the pickup's colour (Pickup:
// the mid hump and treble peak of a single coil or humbucker), adds the body's resonances (Body: the air and top
// resonances of a dreadnought, jumbo, parlor or classical guitar; Resonance scales them), the strings' sparkle
// (Top), and a little width from short reflections. Biquads only: no latency. No config.
extern const BlockDescriptor kAcousticBlockDescriptor;

enum AcousticParam {
    kAcousticParamBody,      // choice: Dreadnought, Jumbo, Parlor, Classical
    kAcousticParamPickup,    // choice: Single Coil, Humbucker, Neutral
    kAcousticParamResonance, // %
    kAcousticParamTop,       // dB
    kAcousticParamBass,      // dB
    kAcousticParamWidth,     // %
    kAcousticParamMix,       // %
    kAcousticParamLevel,     // dB
    kAcousticParamCount
};

} // namespace namsayin
