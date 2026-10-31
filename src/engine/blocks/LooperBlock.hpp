// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

#include <cstdint>

namespace xythos {

// Looper: one loop of up to 60 s (at 48 kHz; the buffer is a fixed number of samples). Footswitch-style controls
// (momentary parameters: map footswitches to them with MIDI learn):
// - Rec/Dub: empty -> record; recording -> play (closes the loop); playing <-> overdub; stopped -> overdub
// - Play/Stop: recording -> play; playing or overdubbing -> stop; stopped -> play from the start
// - Clear: erases the loop
// Feedback is how much of the loop each overdub pass keeps; Reverse and Half Speed change playback. The dry signal
// always passes. meter() is the status (LooperStatus).
//
// Count In mode (owner, 2026-10-30; the default): Rec/Dub on an empty looper starts a metronome (the Classic click,
// at its own BPM, Beats to the bar) that counts in Count-In clicks, then records exactly Measures bars, closes the
// loop on its own and overdubs Loops - 1 more passes; the clicks go on until then and stop for the playback. The
// clicks are added to the output only, never recorded. Rec/Dub or Play/Stop during the count-in cancels it; during
// the timed recording they act as in Free mode (and the clicks stop). Free mode is the looper as it was.
//
// The clicks go to the engine's click bus (Block::setClickOutput()), so the blocks after the looper don't change them.
// Count-In defaults to 8 clicks, two bars of 4 (owner, 2026-10-31; was 4), and Click to 150% (was 70%: too quiet to
// hear while playing). Tap (owner, 2026-10-31): tapping it sets the tempo for the next count-in, from the average of
// the last taps; the UI times the same taps and sets the BPM control to match.
//
// Config: "countIn" (bool) marks a block made since Count In existed. Blocks saved before it lack it: their new
// controls hold the host's unused values, so the DSP keeps them in Free mode, and the UI sets the controls to their
// defaults and adds the key (migrateBlocks()).
extern const BlockDescriptor kLooperBlockDescriptor;

constexpr const char* kLooperCountInKey = "countIn";

enum LooperParam {
    kLooperParamRecDub,   // momentary
    kLooperParamPlayStop, // momentary
    kLooperParamClear,    // momentary
    kLooperParamLevel,    // %
    kLooperParamFeedback, // %
    kLooperParamReverse,  // toggle
    kLooperParamHalf,     // toggle
    kLooperParamMode,     // choice: Count In, Free
    kLooperParamBpm,      // the count-in metronome's tempo
    kLooperParamBeats,    // choice: beats to the bar, 1..12
    kLooperParamCountIn,  // choice: clicks before recording, 0..16
    kLooperParamMeasures, // choice: bars to record, 1..16
    kLooperParamLoops,    // choice: passes recorded (the first, then overdubs), 1..8
    kLooperParamClick,    // %: the clicks' level (100% = the metronome's default volume; up to 200%)
    kLooperParamTap,      // momentary: tap tempo for BPM
    kLooperParamCount
};

enum class LooperState { Empty, Recording, Playing, Overdubbing, Stopped, CountIn };

constexpr double kLooperMaxSeconds = 60.0;
constexpr uint32_t kLooperMaxSamples = 60 * 48000;

// The status the looper reports through meter(): exact in a float (under 2^24)
struct LooperStatus {
    LooperState state = LooperState::Empty;
    int lengthTenths = 0; // loop length in tenths of a second (0..600)
    int progress = 0;     // position in the loop, percent (0..99); Count In mode's recording: how much is done,
                          // percent; counting in: the click being counted (1..16)

    float encode() const
    {
        return static_cast<float>(static_cast<int>(state) * 1000000 + lengthTenths * 100 + progress);
    }

    static LooperStatus decode(float value)
    {
        const int v = static_cast<int>(value + 0.5f);
        LooperStatus s;
        s.state = static_cast<LooperState>(v / 1000000 > 5 ? 0 : v / 1000000);
        s.lengthTenths = (v / 100) % 10000;
        s.progress = v % 100;
        return s;
    }
};

} // namespace xythos
