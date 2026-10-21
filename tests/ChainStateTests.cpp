// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "state/ChainState.hpp"

using namespace namsayin;

TEST_CASE("chain state round-trips through JSON")
{
    ChainState chain;
    BlockState block;
    block.typeId = "test.gain";
    block.slot = 3;
    block.config = { { "file", "My Amp.nam" } };
    block.params[0] = 0.25f;
    block.bypass = true;
    chain.blocks.push_back(block);

    const std::optional<ChainState> parsed = ChainState::fromJson(chain.toJson());
    REQUIRE(parsed);
    CHECK(*parsed == chain);
}

TEST_CASE("empty state is an empty chain; bad JSON is rejected")
{
    const std::optional<ChainState> empty = ChainState::fromJson("");
    REQUIRE(empty);
    CHECK(empty->blocks.empty());

    std::string error;
    CHECK_FALSE(ChainState::fromJson("{not json", &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(ChainState::fromJson(R"({"version": 99, "blocks": []})"));
}

TEST_CASE("validation catches bad slots, unknown types and too many blocks")
{
    const BlockRegistry& registry = testing::testRegistry();

    ChainState chain;
    chain.blocks.push_back({ "test.gain", 0 });
    CHECK(chain.validate(registry));

    chain.blocks.push_back({ "test.gain", 0 });
    CHECK_FALSE(chain.validate(registry));

    chain.blocks.back().slot = kMaxBlocks;
    CHECK_FALSE(chain.validate(registry));

    chain.blocks.back() = { "no.such.block", 1 };
    CHECK_FALSE(chain.validate(registry));

    ChainState full;
    for (int i = 0; i <= kMaxBlocks; ++i)
        full.blocks.push_back({ "test.gain", i % kMaxBlocks });
    CHECK_FALSE(full.validate(registry));
}

TEST_CASE("freeSlot finds the lowest unused slot")
{
    ChainState chain;
    CHECK(chain.freeSlot() == 0);
    chain.blocks.push_back({ "test.gain", 0 });
    chain.blocks.push_back({ "test.gain", 2 });
    CHECK(chain.freeSlot() == 1);

    ChainState full;
    for (int i = 0; i < kMaxBlocks; ++i)
        full.blocks.push_back({ "test.gain", i });
    CHECK(full.freeSlot() == -1);
}

TEST_CASE("MIDI mappings and the preset name round-trip and validate")
{
    ChainState chain;
    chain.blocks.push_back({ "test.gain", 0 });
    chain.preset = "/presets/Clean.namsayin";
    chain.setMidiMapping({ 0, 7, slotParam(0, 0) });
    chain.setMidiMapping({ 2, 11, kParamOutput });
    REQUIRE(chain.midi.size() == 2);

    const std::optional<ChainState> back = ChainState::fromJson(chain.toJson());
    REQUIRE(back);
    CHECK(*back == chain);
    CHECK(back->validate(testing::testRegistry()));

    // one mapping per CC and per parameter
    chain.setMidiMapping({ 0, 7, kParamInput });
    CHECK(chain.midi.size() == 2);
    CHECK(chain.midiMappingFor(slotParam(0, 0)) == nullptr);
    chain.setMidiMapping({ 5, 5, kParamInput });
    CHECK(chain.midi.size() == 2);
    CHECK(chain.midiMappingFor(kParamInput)->cc == 5);
    chain.clearMidiMappings(kParamInput);
    CHECK(chain.midiMappingFor(kParamInput) == nullptr);

    // outputs can't be mapped
    chain.midi.push_back({ 0, 1, kParamOutPeakL });
    CHECK_FALSE(chain.validate(testing::testRegistry()));
}
