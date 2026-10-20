// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/Params.hpp"

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

} // namespace

const GlobalParamSpec& globalParamSpec(uint32_t index)
{
    return kGlobalParams[index];
}

} // namespace namsayin
