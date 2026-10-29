// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/MidiOutput.hpp"
#include "engine/Params.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace namsayin {

// Add Block dialog categories, in display order
enum class Category {
    Nam,
    Ir,
    Di,
    Distortion,
    Dynamics,
    Eq,
    Modulation,
    Reverb,
    Delay,
    Pitch,
    Synth,
    Filter,
    Wah,
};

enum class ParamScale {
    Linear,
    Log,     // min and max must be > 0
    Toggle,
    Choice,  // integer steps from min to max
    Momentary, // 0/1 like a toggle, but the UI shows a button that is 1 only while held (footswitch actions)
};

// One block parameter. The host sees every slot parameter as 0..1; the spec maps that to the real value.
struct ParamSpec {
    const char* name = nullptr;
    const char* unit = "";
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    ParamScale scale = ParamScale::Linear;
    const char* const* choices = nullptr; // Choice only: one label per step, from min to max

    float toValue(float normalized) const
    {
        const float n = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
        switch (scale)
        {
        case ParamScale::Log:
            return min * std::pow(max / min, n);
        case ParamScale::Toggle:
        case ParamScale::Momentary:
            return n >= 0.5f ? 1.0f : 0.0f;
        case ParamScale::Choice:
            return std::round(min + n * (max - min));
        case ParamScale::Linear:
            break;
        }
        return min + n * (max - min);
    }

    float toNormalized(float value) const
    {
        float n;
        switch (scale)
        {
        case ParamScale::Log:
            n = std::log(value / min) / std::log(max / min);
            break;
        case ParamScale::Toggle:
        case ParamScale::Momentary:
            n = value >= 0.5f ? 1.0f : 0.0f;
            break;
        default:
            n = (value - min) / (max - min);
            break;
        }
        return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    }
};

// Base class for every block in the signal chain.
// Blocks always support up to two channels; in mono mode only the first is processed.
class Block {
public:
    virtual ~Block() = default;

    // Non-RT. Called before first use, and again whenever the sample rate or maximum buffer size changes.
    virtual void prepare(double sampleRate, uint32_t maxFrames) = 0;

    // Non-RT. Applies block settings that aren't host parameters (e.g. a NAM file path).
    // Returns false if the block can't be used with this config.
    virtual bool configure(const nlohmann::json& /*config*/) { return true; }

    // RT. `value` is the real value (already mapped through the ParamSpec).
    virtual void setParam(int index, float value) = 0;

    // RT. Processes in place. numChannels is 1 or 2.
    virtual void process(float* const* io, uint32_t numChannels, uint32_t frames) = 0;

    // RT. The host tempo, called before every process(). 120 BPM when the host doesn't report one.
    virtual void setTempo(double /*bpm*/) {}

    // RT. While the host's transport plays: the song position at the start of the coming process() buffer, in
    // quarter notes from the start of the song. Tempo-synced LFOs lock their phase to it, so they land on the beat.
    virtual void setSongPosition(double /*beats*/) {}

    // RT. A level the UI shows on the block (e.g. a compressor's gain reduction in dB); read after process().
    virtual float meter() const { return 0.0f; }

    // Latency in samples this block adds. May only change across a configure()/prepare() call.
    virtual uint32_t latency() const { return 0; }

    // RT. Clears internal state (delay lines, tails).
    virtual void reset() {}

    // RT. The rig's input (after the input level, before the chain), called before every process() with the same
    // frame count; for blocks that listen to it, like a noise gate keyed from the guitar. numChannels is 1 or 2.
    virtual void setRigInput(const float* const* /*input*/, uint32_t /*numChannels*/) {}

    // RT. Where the block sends MIDI (MIDI Out), called before every process(); its frames are the buffer's.
    virtual void setMidiOutput(MidiOutput* /*output*/) {}
};

enum class MeterStyle {
    Reduction, // dB, positive
    Looper,    // LooperStatus::encode()
};

struct BlockDescriptor {
    const char* typeId;      // stored in presets and plugin state; never rename
    const char* displayName;
    Category category;
    int numParams;
    std::array<ParamSpec, kParamsPerSlot> params;
    std::unique_ptr<Block> (*create)();

    // Optional UI hints.
    // A Toggle that makes `syncedParam` pick a note value (TempoSync) instead of its own unit; the knob's top is
    // the longest note for a time and the shortest for a rate.
    int syncParam = -1;
    int syncedParam = -1;
    bool syncedIsRate = false;
    // The UI shows meter() as a gain reduction in dB (0..meterRange) under this name, or as the looper's status
    const char* meterName = nullptr;
    float meterRange = 24.0f;
    MeterStyle meterStyle = MeterStyle::Reduction;
    // The block follows the played note and takes the "lookahead" config (ms; blocks/TrackedInput.hpp): the UI
    // shows a Lookahead button
    bool lookahead = false;
    // The block makes a stereo sound from a mono one (ping-pong echoes): in Mono mode the chain turns stereo at it
    bool stereo = false;
};

} // namespace namsayin
