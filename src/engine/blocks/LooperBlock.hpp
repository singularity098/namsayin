// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

#include <cstdint>

namespace namsayin {

// Looper: one loop of up to 60 s (at 48 kHz; the buffer is a fixed number of samples). Footswitch-style controls
// (momentary parameters: map footswitches to them with MIDI learn):
// - Rec/Dub: empty -> record; recording -> play (closes the loop); playing <-> overdub; stopped -> overdub
// - Play/Stop: recording -> play; playing or overdubbing -> stop; stopped -> play from the start
// - Clear: erases the loop
// Feedback is how much of the loop each overdub pass keeps; Reverse and Half Speed change playback. The dry signal
// always passes. meter() is the status (LooperStatus). No config.
extern const BlockDescriptor kLooperBlockDescriptor;

enum LooperParam {
    kLooperParamRecDub,   // momentary
    kLooperParamPlayStop, // momentary
    kLooperParamClear,    // momentary
    kLooperParamLevel,    // %
    kLooperParamFeedback, // %
    kLooperParamReverse,  // toggle
    kLooperParamHalf,     // toggle
    kLooperParamCount
};

enum class LooperState { Empty, Recording, Playing, Overdubbing, Stopped };

constexpr double kLooperMaxSeconds = 60.0;
constexpr uint32_t kLooperMaxSamples = 60 * 48000;

// The status the looper reports through meter(): exact in a float (under 2^24)
struct LooperStatus {
    LooperState state = LooperState::Empty;
    int lengthTenths = 0; // loop length in tenths of a second (0..600)
    int progress = 0;     // position in the loop, percent (0..99)

    float encode() const
    {
        return static_cast<float>(static_cast<int>(state) * 1000000 + lengthTenths * 100 + progress);
    }

    static LooperStatus decode(float value)
    {
        const int v = static_cast<int>(value + 0.5f);
        LooperStatus s;
        s.state = static_cast<LooperState>(v / 1000000 > 4 ? 0 : v / 1000000);
        s.lengthTenths = (v / 100) % 10000;
        s.progress = v % 100;
        return s;
    }
};

} // namespace namsayin
