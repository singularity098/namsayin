// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

namespace namsayin {

// The metronome's Classic click samples (the ones Fretbored uses), built into the binary.
// ClickSamples.cpp is generated from resources/sounds/ by tools/embed_sounds.py.
struct ClickSample {
    const int16_t* data;
    uint32_t length;
    uint32_t sampleRate;
};

extern const ClickSample kClassicHigh;
extern const ClickSample kClassicLow;

} // namespace namsayin
