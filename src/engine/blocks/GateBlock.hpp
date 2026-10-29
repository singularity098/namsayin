// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Noise gate: a hard gate or a downward expander, with hold, hysteresis and a choice of what it listens to: its
// own input, or the rig input (the guitar before the chain), so a gate after a high-gain amp can follow the clean
// signal. meter() is the current attenuation in dB (positive). No config.
extern const BlockDescriptor kGateBlockDescriptor;

enum GateParam {
    kGateParamThreshold,  // dB
    kGateParamReduction,  // dB of attenuation when closed
    kGateParamAttack,     // ms
    kGateParamHold,       // ms
    kGateParamRelease,    // ms
    kGateParamHysteresis, // dB the level must fall below the threshold to close
    kGateParamType,       // choice: GateType
    kGateParamKey,        // choice: GateKey
    kGateParamCount
};

enum class GateType { Gate, Expander };
enum class GateKey { Block, Rig };

} // namespace xythos
