// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Studio-style feed-forward compressor: soft knee, stereo-linked peak detection, makeup gain and a parallel
// (dry/wet) mix. Its meter() is the current gain reduction in dB (positive). No config.
extern const BlockDescriptor kCompressorBlockDescriptor;

enum CompressorParam {
    kCompParamThreshold, // dB
    kCompParamRatio,     // :1
    kCompParamAttack,    // ms
    kCompParamRelease,   // ms
    kCompParamKnee,      // dB
    kCompParamMakeup,    // dB
    kCompParamMix,       // %
    kCompParamCount
};

// Static gain curve: output level in dB for an input level in dB (no attack/release)
float compressorCurve(float inputDb, float thresholdDb, float ratio, float kneeDb);

} // namespace xythos
