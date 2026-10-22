// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

namespace namsayin {

// Plain-English help for the controls, shown in the UI's hover popups. Plain data, no DPF, so the tests can check
// that every control has one.

// A block type: what it is, what it's for and how to use it, by typeId; nullptr if there's none
const char* blockTypeHelp(const char* typeId);

// A block's control, by the block's typeId and the control's ParamSpec name; nullptr if there's none
const char* blockParamHelp(const char* typeId, const char* paramName);

// A control that isn't a block's (global knobs, merge, tuner, metronome), by host parameter index; nullptr if none
const char* globalParamHelp(uint32_t index);

} // namespace namsayin
