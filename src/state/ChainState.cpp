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
    json midiJson = json::array();
    for (const MidiMapping& mapping : midi)
        midiJson.push_back({ { "channel", mapping.channel }, { "cc", mapping.cc }, { "param", mapping.param } });
    json root { { "version", kVersion }, { "blocks", blocksJson } };
    if (!midi.empty())
        root["midi"] = midiJson;
    if (!preset.empty())
        root["preset"] = preset;
    return root.dump();
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
        if (const auto midi = root.find("midi"); midi != root.end())
        {
            for (const json& item : *midi)
            {
                state.midi.push_back({ item.at("channel").get<int>(), item.at("cc").get<int>(),
                                       item.at("param").get<uint32_t>() });
            }
        }
        state.preset = root.value("preset", std::string());
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
    for (const MidiMapping& mapping : midi)
    {
        if (mapping.channel < 0 || mapping.channel > 15 || mapping.cc < 0 || mapping.cc > 127)
            return fail("invalid MIDI mapping");
        if (mapping.param >= kParamCount || !isMidiMappable(mapping.param))
            return fail("MIDI mapping to a parameter that can't be mapped: " + std::to_string(mapping.param));
    }
    return true;
}

void ChainState::setMidiMapping(MidiMapping mapping)
{
    std::erase_if(midi, [&mapping](const MidiMapping& m) {
        return (m.channel == mapping.channel && m.cc == mapping.cc) || m.param == mapping.param;
    });
    midi.push_back(mapping);
}

void ChainState::clearMidiMappings(uint32_t param)
{
    std::erase_if(midi, [param](const MidiMapping& m) { return m.param == param; });
}

const MidiMapping* ChainState::midiMappingFor(uint32_t param) const
{
    for (const MidiMapping& mapping : midi)
    {
        if (mapping.param == param)
            return &mapping;
    }
    return nullptr;
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
