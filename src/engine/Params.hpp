// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

namespace namsayin {

constexpr int kMaxBlocks = 32;
constexpr int kParamsPerSlot = 8;
constexpr int kSlotParamCount = 1 + kParamsPerSlot; // bypass + block parameters

// Host parameter indices. These are the DAW's automation IDs, so existing indices must never move:
// parameters added in later milestones go after the slots (see kFirstLaterParam).
enum GlobalParam : uint32_t {
    kParamInputMode,        // 0 = mono, 1 = stereo
    kParamInput,            // dB
    kParamBass,             // dB
    kParamMiddle,           // dB
    kParamTreble,           // dB
    kParamOutput,           // dB
    kParamBypass,           // global bypass
    kParamLimiter,          // output limiter on/off

    // outputs (DSP -> UI)
    kParamInPeakL,          // dBFS
    kParamInPeakR,
    kParamOutPeakL,
    kParamOutPeakR,
    kParamInClip,           // 1 while the input has recently clipped
    kParamOutClip,
    kParamLimiterReduction, // dB of gain reduction (positive)

    kGlobalParamCount
};

constexpr uint32_t kSlotParamBase = kGlobalParamCount;
constexpr uint32_t kFirstLaterParam = kSlotParamBase + kMaxBlocks * kSlotParamCount;

// Parameters added after the slot block. Append only.
enum LaterParam : uint32_t {
    // outputs: which slots have a block running in the audio engine, as two 16-bit masks (floats hold these exactly)
    kParamRunningSlotsLow = kFirstLaterParam,
    kParamRunningSlotsHigh,

    // chain exchange for hosts that don't deliver UI state (see state/ChainExchange.hpp)
    kParamInstanceId,       // output: this plugin instance's random ID
    kParamChainRevision,    // input, hidden: bumped by the UI after writing the exchange file

    // merge of the split (M6)
    kParamMergeBalance,     // -100 (only A) .. 100 (only B) %; both at full level in the middle
    kParamMergeLevelA,      // dB
    kParamMergeLevelB,
    kParamMergePanA,        // -100 (left) .. 100 (right) %; stereo mode only
    kParamMergePanB,
    kParamMergeInvertA,     // phase invert
    kParamMergeInvertB,

    // tuner (M8)
    kParamTuner,            // on/off
    kParamTunerMute,        // mute the guitar while the tuner is on
    kParamTunerReference,   // A4 in Hz
    kParamTunerStrobe,      // hidden, UI only: strobe view instead of the needle
    kParamTunerFrequency,   // output: detected frequency in Hz, 0 when there's no clear note
    kParamTunerClarity,     // output: 0..1

    kParamCount
};

constexpr uint32_t slotBypassParam(int slot)
{
    return kSlotParamBase + static_cast<uint32_t>(slot * kSlotParamCount);
}

constexpr uint32_t slotParam(int slot, int param)
{
    return slotBypassParam(slot) + 1 + static_cast<uint32_t>(param);
}

constexpr bool isSlotParam(uint32_t index)
{
    return index >= kSlotParamBase && index < kFirstLaterParam;
}

enum ParamFlags : uint32_t {
    kParamFlagNone    = 0,
    kParamFlagOutput  = 1 << 0,
    kParamFlagToggle  = 1 << 1,
    kParamFlagInteger = 1 << 2,
    kParamFlagHidden  = 1 << 3,
};

struct GlobalParamSpec {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    uint32_t flags;
};

// Spec for any parameter that isn't a slot parameter (globals and later parameters)
const GlobalParamSpec& globalParamSpec(uint32_t index);

} // namespace namsayin
