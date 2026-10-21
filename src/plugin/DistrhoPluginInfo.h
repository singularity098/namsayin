// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#define DISTRHO_PLUGIN_BRAND   "singularity098"
#define DISTRHO_PLUGIN_NAME    "NAMsayin"
#define DISTRHO_PLUGIN_URI     "https://github.com/singularity098/namsayin"
#define DISTRHO_PLUGIN_CLAP_ID "com.github.singularity098.namsayin"

#define DISTRHO_PLUGIN_BRAND_ID  S098
#define DISTRHO_PLUGIN_UNIQUE_ID NMsy

#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_WANT_STATE   1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_LATENCY 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS 1
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
// MIDI-mapped parameter changes are reported back to the host (VST3; LV2 hosts with ctrlInPortChangeRequest)
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST 1

#define DISTRHO_UI_USE_NANOVG      1
#define DISTRHO_UI_FILE_BROWSER    1
#define DISTRHO_UI_USER_RESIZABLE  1
#define DISTRHO_UI_DEFAULT_WIDTH   1200
#define DISTRHO_UI_DEFAULT_HEIGHT  720

// Smallest window size, in logical (unscaled) pixels
#define NAMSAYIN_UI_MIN_WIDTH  900
#define NAMSAYIN_UI_MIN_HEIGHT 560
