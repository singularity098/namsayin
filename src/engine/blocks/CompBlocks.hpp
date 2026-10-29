// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace namsayin {

// Compressor pedals (owner, 2026-10-29: "half a dozen compressor pedals", Neve-based circuits and optical compressors
// among them). Each is the way its circuit compresses (the topology, the detector, the gain cell's release) and the
// colour of its stages (transformers, a class-A preamp, the diode bridge, the FET, the tube, the OTA), not a capture.
// The clipping stages run at 4x (zero-latency IIR oversampling). Detection is stereo-linked. meter() is the gain
// reduction in dB. No config. Each one's parameter order is fixed from its release (automation follows it).

// Diode Bridge: a Neve 33609-style bus compressor: a feedback diode-bridge compressor (its distortion grows with the
// gain reduction) between transformers, a stepped Ratio, a Recovery switch with two program-dependent Auto settings,
// and a limiter after it (Limit: its threshold).
extern const BlockDescriptor kDiodeBridgeBlockDescriptor;
enum DiodeBridgeParam {
    kBridgeThreshold, kBridgeRatio, kBridgeRecovery, kBridgeLimit, kBridgeGain, kBridgeMix, kBridgeParamCount
};

// Console Comp: a Neve channel: a class-A transistor preamp with its transformers (Drive) into a 2254-style
// feedback diode-bridge compressor with Attack and Release, then Level.
extern const BlockDescriptor kConsoleCompBlockDescriptor;
enum ConsoleCompParam {
    kConsoleDrive, kConsoleThreshold, kConsoleRatio, kConsoleAttack, kConsoleRelease, kConsoleLevel, kConsoleMix,
    kConsoleParamCount
};

// Opto Leveler: an LA-2A-style optical levelling amp: Peak Reduction, Gain, Compress/Limit, Emphasis (the side
// chain's treble), Mix. A feedback design whose electro-optical cell attacks in about 10 ms and releases in two
// stages, the second slower the longer it has been working; a tube output stage.
extern const BlockDescriptor kOptoLevelerBlockDescriptor;
enum OptoLevelerParam {
    kLevelerPeakReduction, kLevelerGain, kLevelerMode, kLevelerEmphasis, kLevelerMix, kLevelerParamCount
};

// Opto Stomp: an optical stompbox compressor: an LED lights a photoresistor that shunts the signal, so the ratio grows
// gently with the level, the attack is slow enough to let the pick through, and the release slows as the cell darkens.
// Sustain, Tone, Level, Blend.
extern const BlockDescriptor kOptoStompBlockDescriptor;
enum OptoStompParam { kOptoStompSustain, kOptoStompTone, kOptoStompLevel, kOptoStompBlend, kOptoStompParamCount };

// FET Limiter: an 1176-style FET compressor: Input drives it into a fixed threshold (the level is compensated, so
// Output is the make-up), very fast Attack (0.02-0.8 ms), Release, Ratio buttons including All (all buttons in: it
// over-compresses, and distorts), a class-A output stage that grits up when pushed.
extern const BlockDescriptor kFetLimiterBlockDescriptor;
enum FetLimiterParam {
    kFetInput, kFetOutput, kFetAttack, kFetRelease, kFetRatio, kFetMix, kFetParamCount
};

// Squash Comp: an OTA stompbox compressor (the classic country squash): quiet notes come up by up to Sustain, loud
// ones are held near a ceiling; a fast or slow Attack, the OTA's soft clipping and its darker top; Level, Blend.
extern const BlockDescriptor kSquashCompBlockDescriptor;
enum SquashCompParam { kSquashSustain, kSquashAttack, kSquashLevel, kSquashBlend, kSquashParamCount };

} // namespace namsayin
