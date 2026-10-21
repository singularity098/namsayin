// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/Params.hpp"

#include <cstdio>

namespace namsayin {

namespace {

constexpr GlobalParamSpec kGlobalParams[kGlobalParamCount] = {
    { "Input Mode",        "input_mode",  "",   0.0f,   1.0f,   0.0f, kParamFlagToggle },
    { "Input",             "input",       "dB", -24.0f, 24.0f,  0.0f, kParamFlagNone },
    { "Bass",              "bass",        "dB", -12.0f, 12.0f,  0.0f, kParamFlagNone },
    { "Middle",            "middle",      "dB", -12.0f, 12.0f,  0.0f, kParamFlagNone },
    { "Treble",            "treble",      "dB", -12.0f, 12.0f,  0.0f, kParamFlagNone },
    { "Output",            "output",      "dB", -24.0f, 24.0f,  0.0f, kParamFlagNone },
    { "Bypass",            "bypass",      "",   0.0f,   1.0f,   0.0f, kParamFlagToggle },
    { "Limiter",           "limiter",     "",   0.0f,   1.0f,   1.0f, kParamFlagToggle },
    { "Input Peak L",      "in_peak_l",   "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Input Peak R",      "in_peak_r",   "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Output Peak L",     "out_peak_l",  "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Output Peak R",     "out_peak_r",  "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Input Clip",        "in_clip",     "",   0.0f,   1.0f,   0.0f, kParamFlagOutput | kParamFlagToggle },
    { "Output Clip",       "out_clip",    "",   0.0f,   1.0f,   0.0f, kParamFlagOutput | kParamFlagToggle },
    { "Limiter Reduction", "limiter_gr",  "dB", 0.0f,   24.0f,  0.0f, kParamFlagOutput },
};

// Fixed part of the later parameters; the slot meters are generated in globalParamSpec()
constexpr GlobalParamSpec kLaterParams[kParamSlotMeterBase - kFirstLaterParam] = {
    { "Running Slots 1-16",  "running_lo", "", 0.0f, 65535.0f, 0.0f, kParamFlagOutput | kParamFlagInteger },
    { "Running Slots 17-32", "running_hi", "", 0.0f, 65535.0f, 0.0f, kParamFlagOutput | kParamFlagInteger },
    { "Instance ID",         "instance_id", "", 0.0f, 16777215.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
    { "Chain Revision",      "chain_rev",   "", 0.0f, 16777215.0f, 0.0f, kParamFlagInteger | kParamFlagHidden },
};

constexpr GlobalParamSpec kMidiParams[kParamCount - kParamSlotMeterEnd] = {
    { "MIDI Learn",    "midi_learn",    "", 0.0f, 1.0f, 0.0f, kParamFlagToggle | kParamFlagHidden },
    { "MIDI Last CC",  "midi_last_cc",  "", 0.0f, 2047.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
    { "MIDI CC Count", "midi_cc_count", "", 0.0f, 16777215.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
    { "MIDI Last Value", "midi_last_value", "", 0.0f, 127.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
};

struct SlotMeterSpecs {
    char names[kMaxBlocks][24];
    char symbols[kMaxBlocks][16];
    GlobalParamSpec specs[kMaxBlocks];

    SlotMeterSpecs()
    {
        for (int slot = 0; slot < kMaxBlocks; ++slot)
        {
            std::snprintf(names[slot], sizeof(names[slot]), "Slot %02d Meter", slot + 1);
            std::snprintf(symbols[slot], sizeof(symbols[slot]), "s%02d_meter", slot + 1);
            specs[slot] = { names[slot], symbols[slot], "", -60.0f, 60.0f, 0.0f, kParamFlagOutput | kParamFlagHidden };
        }
    }
};

} // namespace

const GlobalParamSpec& globalParamSpec(uint32_t index)
{
    if (index >= kParamSlotMeterBase)
    {
        static const SlotMeterSpecs meters;
        return index < kParamSlotMeterEnd ? meters.specs[index - kParamSlotMeterBase]
                                          : kMidiParams[index - kParamSlotMeterEnd];
    }
    return index >= kFirstLaterParam ? kLaterParams[index - kFirstLaterParam] : kGlobalParams[index];
}

float midiCcToValue(uint32_t index, int ccValue)
{
    const int clamped = ccValue < 0 ? 0 : (ccValue > 127 ? 127 : ccValue);
    const float n = clamped / 127.0f;
    if (isSlotParam(index))
    {
        // slot bypass is a switch; block parameters are normalized
        return (index - kSlotParamBase) % kSlotParamCount == 0 ? (clamped >= 64 ? 1.0f : 0.0f) : n;
    }
    const GlobalParamSpec& spec = globalParamSpec(index);
    if (spec.flags & kParamFlagToggle)
        return clamped >= 64 ? spec.max : spec.min;
    return spec.min + n * (spec.max - spec.min);
}

} // namespace namsayin
