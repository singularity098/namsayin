// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Params.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace namsayin {

class BlockRegistry;

struct BlockState {
    std::string typeId;
    int slot = -1;                                // fixed host parameter slot, 0..kMaxBlocks-1
    nlohmann::json config = nlohmann::json::object(); // non-automatable settings, e.g. a file path
    std::array<float, kParamsPerSlot> params {};   // normalized 0..1 values, for presets and undo
    bool bypass = false;

    bool operator==(const BlockState&) const = default;
};

// A MIDI CC mapped to a host parameter (MIDI learn)
struct MidiMapping {
    int channel = 0; // 0..15
    int cc = 0;      // 0..127
    uint32_t param = 0; // host parameter index (Params.hpp)

    bool operator==(const MidiMapping&) const = default;
};

// The whole signal chain as plain data. Shared by the DSP (plugin state), the UI, presets and undo.
// The DSP takes parameter values from the host, not from `params`/`bypass`; the UI pushes those to the host.
struct ChainState {
    static constexpr int kVersion = 1;

    std::vector<BlockState> blocks; // left to right
    std::vector<MidiMapping> midi;  // at most one mapping per CC and channel
    std::string preset;             // file of the preset this came from (UTF-8), empty if none

    std::string toJson() const;
    static std::optional<ChainState> fromJson(std::string_view json, std::string* error = nullptr);

    // Checks block count, slot range and uniqueness, and that every type exists
    bool validate(const BlockRegistry& registry, std::string* error = nullptr) const;

    // Lowest slot not used by any block, or -1 if the chain is full
    int freeSlot() const;

    // Maps a CC to a parameter, replacing any mapping of that CC or to that parameter
    void setMidiMapping(MidiMapping mapping);
    void clearMidiMappings(uint32_t param);
    const MidiMapping* midiMappingFor(uint32_t param) const;

    bool operator==(const ChainState&) const = default;
};

} // namespace namsayin
