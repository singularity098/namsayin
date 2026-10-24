// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// The DI category (owner, 2026-10-24): bass DI boxes and bass preamps, the box between a bass and the desk. Unlike
// guitar amps they are a few stages each (a transformer, a tube or op-amp gain stage with its clipping, a tone stack,
// sometimes a speaker voicing, a clean blend or a crossover), so they're built from filters and oversampled clipping
// curves, not captured. Their clipping runs at 4x (zero-latency IIR oversampling, like the drive pedals). No config.
// Each one's parameter order is fixed from its release (automation follows it).

// Studio DI: a passive transformer DI. Iron saturates the lows the way a transformer core does when pushed (the
// highs stay clean); Low Cut and Air trim the ends.
extern const BlockDescriptor kStudioDiBlockDescriptor;
enum StudioDiParam { kStudioDiIron, kStudioDiLowCut, kStudioDiAir, kStudioDiLevel, kStudioDiParamCount };

// Valve DI: a studio tube DI with a voicing switch (Flat, Deep, Bright, Mid, Scoop, Vintage) and Warmth (how hard
// the tube is driven).
extern const BlockDescriptor kValveDiBlockDescriptor;
enum ValveDiParam { kValveDiCharacter, kValveDiWarmth, kValveDiLevel, kValveDiParamCount };

// Tube Preamp: a classic all-tube bass amp's preamp (8x10 rock tone): Gain into a tube stage, a passive tone stack
// with a five-way mid frequency, Ultra Lo (deep lows, scooped mids) and Ultra Hi switches, then a second tube stage.
extern const BlockDescriptor kTubePreampBlockDescriptor;
enum TubePreampParam {
    kTubePreGain, kTubePreBass, kTubePreMid, kTubePreMidFreq, kTubePreTreble, kTubePreUltraLo, kTubePreUltraHi,
    kTubePreLevel, kTubePreParamCount
};

// Driver DI: a tube-amp emulating DI pedal: a driven amp with a built-in speaker voicing, blended with the clean bass,
// and Presence on the driven side; Bass and Treble after the blend.
extern const BlockDescriptor kDriverDiBlockDescriptor;
enum DriverDiParam {
    kDriverDrive, kDriverBlend, kDriverPresence, kDriverBass, kDriverTreble, kDriverLevel, kDriverParamCount
};

// Modern Drive: a modern high-gain bass preamp: Grunt (lows into the drive) and Attack (highs into the drive), a
// clean blend, and a four-band EQ after it.
extern const BlockDescriptor kModernDriveBlockDescriptor;
enum ModernDriveParam {
    kModernDrive, kModernBlend, kModernGrunt, kModernAttack, kModernBass, kModernLoMid, kModernHiMid, kModernTreble,
    kModernLevel, kModernParamCount
};

// Crossover Drive: splits the bass at Crossover (Linkwitz-Riley, so the bands sum back flat): the lows stay clean
// (Lows sets their level), only the highs are driven (Drive, Tone, Highs). Tight lows at any gain.
extern const BlockDescriptor kCrossoverDriveBlockDescriptor;
enum CrossoverDriveParam {
    kXoverFreq, kXoverLows, kXoverDrive, kXoverTone, kXoverHighs, kXoverLevel, kXoverParamCount
};

// Dual Drive: two channels mixed, clean and distorted (op-amp gain into hard-clipping diodes), each with its level;
// Color scoops the distorted channel; Bass, Mid and Treble shape the mix.
extern const BlockDescriptor kDualDriveBlockDescriptor;
enum DualDriveParam {
    kDualClean, kDualDrive, kDualDist, kDualColor, kDualBass, kDualMid, kDualTreble, kDualLevel, kDualParamCount
};

// Hi-Fi Preamp: a clean modern preamp: an optical-style compressor (Comp), a gentle tube stage (Gain), and an EQ with
// a sweepable mid. For slap and fingerstyle definition.
extern const BlockDescriptor kHiFiPreampBlockDescriptor;
enum HiFiPreampParam {
    kHiFiGain, kHiFiComp, kHiFiBass, kHiFiMid, kHiFiMidFreq, kHiFiTreble, kHiFiLevel, kHiFiParamCount
};

// British Preamp: a punchy British solid-state bass preamp: Gain, a Pre Shape switch (the classic mid scoop) and a
// five-band graphic EQ.
extern const BlockDescriptor kBritishPreampBlockDescriptor;
enum BritishPreampParam {
    kBritGain, kBritPreShape, kBrit63, kBrit200, kBrit600, kBrit1k6, kBrit5k, kBritLevel, kBritParamCount
};

// Opto DI: an optical compressor DI: Peak Reduction (how much it levels), Gain (make-up), Mix (parallel compression)
// and Bright. Slow, smooth, program-dependent release. Shows the gain reduction.
extern const BlockDescriptor kOptoDiBlockDescriptor;
enum OptoDiParam { kOptoPeakReduction, kOptoGain, kOptoMix, kOptoBright, kOptoParamCount };

} // namespace namsayin
