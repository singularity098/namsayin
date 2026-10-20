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

// The whole signal chain as plain data. Shared by the DSP (plugin state), the UI, presets and undo.
// The DSP takes parameter values from the host, not from `params`/`bypass`; the UI pushes those to the host.
struct ChainState {
    static constexpr int kVersion = 1;

    std::vector<BlockState> blocks; // left to right

    std::string toJson() const;
    static std::optional<ChainState> fromJson(std::string_view json, std::string* error = nullptr);

    // Checks block count, slot range and uniqueness, and that every type exists
    bool validate(const BlockRegistry& registry, std::string* error = nullptr) const;

    // Lowest slot not used by any block, or -1 if the chain is full
    int freeSlot() const;

    bool operator==(const ChainState&) const = default;
};

} // namespace namsayin
