// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/NoteTracker.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace namsayin {

// What the pitch-following pedals share around the note tracker: Track From (the pedal's input, or the rig's input:
// the clean guitar, much easier to follow than the output of an amp or drive), and Lookahead (block config
// "lookahead", ms: the pedal's audio is delayed to match the tracker's output, and the delay is its latency).
class TrackedInput {
public:
    static constexpr const char* kTrackNames[] = { "Block In", "Rig In" };
    static constexpr const char* kConfigKey = "lookahead";

    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fNotes.prepare(sampleRate);
        const auto size = static_cast<size_t>(std::ceil(NoteTracker::kMaxLookaheadMs / 1000.0 * sampleRate)) + 2;
        for (std::vector<float>& line : fLines)
            line.assign(size, 0.0f);
        fPos = 0;
    }

    // Non-RT: reads the lookahead from a block config
    void configure(const nlohmann::json& config)
    {
        const double ms = config.is_object() ? config.value(kConfigKey, 0.0) : 0.0;
        fNotes.setLookahead(ms);
        reset();
    }

    uint32_t latency() const { return fNotes.latency(); }

    void setTrackRig(float choice) { fTrackRig = choice >= 0.5f; }
    void setPickFilter(float ms) { fNotes.setPickFilter(ms); }

    // RT, before each process()
    void setRig(const float* const* input, uint32_t numChannels)
    {
        fRig[0] = input[0];
        fRig[1] = numChannels > 1 ? input[1] : input[0];
    }

    void reset()
    {
        fNotes.reset();
        for (std::vector<float>& line : fLines)
            std::fill(line.begin(), line.end(), 0.0f);
        fPos = 0;
    }

    // RT. Sample i of the buffer: tracks it (or the rig's input), and delays io[..][i] by the lookahead in place.
    // Returns the note at the (delayed) output.
    const NoteTracker::Note& process(float* const* io, uint32_t numChannels, uint32_t i)
    {
        float in;
        if (fTrackRig && fRig[0] != nullptr)
            in = 0.5f * (fRig[0][i] + fRig[1][i]);
        else
            in = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
        const NoteTracker::Note& note = fNotes.process(in);
        const auto delay = static_cast<size_t>(fNotes.latency());
        if (delay > 0)
        {
            const size_t size = fLines[0].size();
            const size_t read = (fPos + size - delay) % size;
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                fLines[ch][fPos] = io[ch][i];
                io[ch][i] = fLines[ch][read];
            }
            fPos = (fPos + 1) % size;
        }
        return note;
    }

    const NoteTracker::Note& note() const { return fNotes.note(); }

private:
    double fSampleRate = 48000.0;
    NoteTracker fNotes;
    bool fTrackRig = false;
    const float* fRig[2] = { nullptr, nullptr };
    std::vector<float> fLines[2];
    size_t fPos = 0;
};

} // namespace namsayin
