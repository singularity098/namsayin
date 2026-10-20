// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/ChainState.hpp"

#include "engine/BlockRegistry.hpp"

namespace namsayin {

using nlohmann::json;

std::string ChainState::toJson() const
{
    json blocksJson = json::array();
    for (const BlockState& block : blocks)
    {
        blocksJson.push_back({
            { "type", block.typeId },
            { "slot", block.slot },
            { "config", block.config },
            { "params", block.params },
            { "bypass", block.bypass },
        });
    }
    return json { { "version", kVersion }, { "blocks", blocksJson } }.dump();
}

std::optional<ChainState> ChainState::fromJson(std::string_view text, std::string* error)
{
    // An empty state means an empty chain (e.g. a fresh plugin instance)
    if (text.empty())
        return ChainState {};

    try
    {
        const json root = json::parse(text);
        const int version = root.value("version", 0);
        if (version < 1 || version > kVersion)
        {
            if (error)
                *error = "unsupported chain version " + std::to_string(version);
            return std::nullopt;
        }

        ChainState state;
        for (const json& item : root.at("blocks"))
        {
            BlockState block;
            block.typeId = item.at("type").get<std::string>();
            block.slot = item.at("slot").get<int>();
            block.config = item.value("config", json::object());
            block.bypass = item.value("bypass", false);
            if (const auto params = item.find("params"); params != item.end())
            {
                for (size_t i = 0; i < params->size() && i < block.params.size(); ++i)
                    block.params[i] = (*params)[i].get<float>();
            }
            state.blocks.push_back(std::move(block));
        }
        return state;
    }
    catch (const json::exception& e)
    {
        if (error)
            *error = e.what();
        return std::nullopt;
    }
}

bool ChainState::validate(const BlockRegistry& registry, std::string* error) const
{
    auto fail = [error](std::string message) {
        if (error)
            *error = std::move(message);
        return false;
    };

    if (blocks.size() > static_cast<size_t>(kMaxBlocks))
        return fail("too many blocks");

    std::array<bool, kMaxBlocks> used {};
    for (const BlockState& block : blocks)
    {
        if (block.slot < 0 || block.slot >= kMaxBlocks)
            return fail("slot out of range: " + std::to_string(block.slot));
        if (used[block.slot])
            return fail("slot used twice: " + std::to_string(block.slot));
        used[block.slot] = true;
        if (registry.find(block.typeId) == nullptr)
            return fail("unknown block type: " + block.typeId);
    }
    return true;
}

int ChainState::freeSlot() const
{
    std::array<bool, kMaxBlocks> used {};
    for (const BlockState& block : blocks)
    {
        if (block.slot >= 0 && block.slot < kMaxBlocks)
            used[block.slot] = true;
    }
    for (int slot = 0; slot < kMaxBlocks; ++slot)
    {
        if (!used[slot])
            return slot;
    }
    return -1;
}

} // namespace namsayin
