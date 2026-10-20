// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/Engine.hpp"

#include <cmath>
#include <thread>
#include <vector>

using namespace namsayin;

namespace {

constexpr double kRate = 48000.0;
constexpr uint32_t kBuffer = 256;

// Runs the engine for `buffers` buffers of a constant input and returns the last output sample per channel
struct Rig {
    Engine engine { testing::testRegistry() };
    std::vector<float> inL = std::vector<float>(kBuffer, 0.25f);
    std::vector<float> inR = std::vector<float>(kBuffer, -0.125f);
    std::vector<float> outL = std::vector<float>(kBuffer, 0.0f);
    std::vector<float> outR = std::vector<float>(kBuffer, 0.0f);

    Rig() { engine.prepare(kRate, kBuffer); }

    void run(int buffers = 40)
    {
        const float* in[2] = { inL.data(), inR.data() };
        float* out[2] = { outL.data(), outR.data() };
        for (int i = 0; i < buffers; ++i)
            engine.process(in, out, kBuffer);
    }

    float left() const { return outL.back(); }
    float right() const { return outR.back(); }
};

ChainState gainChain(int slot, nlohmann::json config = nlohmann::json::object())
{
    ChainState chain;
    BlockState block;
    block.typeId = "test.gain";
    block.slot = slot;
    block.config = std::move(config);
    chain.blocks.push_back(block);
    return chain;
}

// Normalized value of the test block's gain parameter for a gain in dB
float gainParam(float db)
{
    return testing::kGainDescriptor.params[0].toNormalized(db);
}

} // namespace

TEST_CASE("empty chain passes audio through; mono mode copies the left input to both outputs")
{
    Rig rig;
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f));
    CHECK(rig.right() == doctest::Approx(0.25f));

    rig.engine.setParameter(kParamInputMode, 1.0f);
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f));
    CHECK(rig.right() == doctest::Approx(-0.125f));
}

TEST_CASE("input and output levels")
{
    Rig rig;
    rig.engine.setParameter(kParamLimiter, 0.0f);
    rig.engine.setParameter(kParamInput, 6.0f);
    rig.engine.setParameter(kParamOutput, -12.0f);
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f * dbToGain(-6.0f)).epsilon(0.001));
}

TEST_CASE("a block's slot parameters reach it after the chain swap")
{
    Rig rig;
    rig.engine.setParameter(slotParam(5, 0), gainParam(-6.0f));
    REQUIRE(rig.engine.setChain(gainChain(5)));
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f * dbToGain(-6.0f)).epsilon(0.001));

    // automation of the slot parameter
    rig.engine.setParameter(slotParam(5, 0), gainParam(-12.0f));
    rig.run(2);
    CHECK(rig.left() == doctest::Approx(0.25f * dbToGain(-12.0f)).epsilon(0.001));
}

TEST_CASE("unchanged blocks are reused when the chain is rebuilt")
{
    Rig rig;
    testing::GainBlock::sCreated = 0;
    REQUIRE(rig.engine.setChain(gainChain(0)));
    rig.run(4);
    REQUIRE(rig.engine.setChain(gainChain(0)));
    rig.run(4);
    CHECK(testing::GainBlock::sCreated == 1);

    // a config change makes a new block
    REQUIRE(rig.engine.setChain(gainChain(0, { { "latency", 0 }, { "x", 1 } })));
    rig.run(4);
    CHECK(testing::GainBlock::sCreated == 2);
}

TEST_CASE("slot bypass and global bypass")
{
    Rig rig;
    rig.engine.setParameter(slotParam(0, 0), gainParam(-6.0f));
    REQUIRE(rig.engine.setChain(gainChain(0)));
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f * dbToGain(-6.0f)).epsilon(0.001));

    rig.engine.setParameter(slotBypassParam(0), 1.0f);
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f));

    // global bypass ignores everything, including the input level
    rig.engine.setParameter(slotBypassParam(0), 0.0f);
    rig.engine.setParameter(kParamInput, 12.0f);
    rig.engine.setParameter(kParamBypass, 1.0f);
    rig.run();
    CHECK(rig.left() == 0.25f);
    CHECK(rig.right() == -0.125f);
}

TEST_CASE("chain latency is reported")
{
    Rig rig;
    CHECK(rig.engine.latency() == 0);
    REQUIRE(rig.engine.setChain(gainChain(0, { { "latency", 64 } })));
    rig.run(4);
    CHECK(rig.engine.latency() == 64);
}

TEST_CASE("a block that fails to configure is left out and reported")
{
    Rig rig;
    rig.engine.setParameter(slotParam(2, 0), gainParam(-6.0f));
    std::vector<int> failed;
    REQUIRE(rig.engine.setChain(gainChain(2, { { "fail", true } }), nullptr, &failed));
    CHECK(failed == std::vector<int> { 2 });
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f));
}

TEST_CASE("an invalid chain is rejected and the old one keeps playing")
{
    Rig rig;
    rig.engine.setParameter(slotParam(0, 0), gainParam(-6.0f));
    REQUIRE(rig.engine.setChain(gainChain(0)));
    rig.run();

    std::string error;
    ChainState bad = gainChain(0);
    bad.blocks[0].typeId = "missing";
    CHECK_FALSE(rig.engine.setChain(bad, &error));
    CHECK_FALSE(error.empty());
    rig.run();
    CHECK(rig.left() == doctest::Approx(0.25f * dbToGain(-6.0f)).epsilon(0.001));
}

TEST_CASE("the output limiter and meters")
{
    Rig rig;
    rig.inL.assign(kBuffer, 0.9f);
    rig.engine.setParameter(kParamOutput, 12.0f);
    rig.run();
    CHECK(rig.left() <= 0.892f);
    CHECK(rig.engine.getParameter(kParamLimiterReduction) > 6.0f);
    CHECK(rig.engine.getParameter(kParamOutPeakL) < 0.0f);
    CHECK(rig.engine.getParameter(kParamInPeakL) == doctest::Approx(gainToDb(0.9f)).epsilon(0.01));

    rig.engine.setParameter(kParamLimiter, 0.0f);
    rig.run();
    CHECK(rig.left() > 1.0f);
    CHECK(rig.engine.getParameter(kParamOutClip) == 1.0f);
}

TEST_CASE("the audio thread never allocates, including across chain swaps")
{
    Rig rig;
    rig.run(1);
    for (int i = 0; i < 20; ++i)
    {
        // alternate between two different blocks so every swap creates and retires one
        REQUIRE(rig.engine.setChain(gainChain(i % 2, { { "n", i } })));
        rig.engine.setParameter(slotParam(i % 2, 0), gainParam(static_cast<float>(-i)));
        rig.engine.setParameter(kParamBass, static_cast<float>(i % 12));

        testing::AllocationGuard guard;
        rig.run(4);
        CHECK(guard.count() == 0);
    }
}

TEST_CASE("engine processes buffers larger than its prepared size")
{
    Engine engine { testing::testRegistry() };
    engine.prepare(kRate, 64);
    std::vector<float> in(1000, 0.5f), out(1000, 0.0f);
    const float* ins[2] = { in.data(), in.data() };
    float* outs[2] = { out.data(), out.data() };
    engine.process(ins, outs, 1000);
    CHECK(out.back() == doctest::Approx(0.5f));
}

TEST_CASE("the allocation guard detects allocations")
{
    static std::vector<float>* volatile sink = nullptr;
    testing::AllocationGuard guard;
    sink = new std::vector<float>(16); // two allocations: the vector and its storage
    CHECK(guard.count() == 2);
    delete sink;
}

TEST_CASE("chain edits on another thread while audio runs")
{
    Rig rig;
    std::atomic<bool> running { true };
    std::thread editor([&] {
        for (int i = 0; running && i < 2000; ++i)
        {
            ChainState chain = gainChain(i % kMaxBlocks, { { "n", i % 3 } });
            if (i % 5 == 0)
                chain.blocks.push_back({ "test.gain", (i + 1) % kMaxBlocks });
            rig.engine.setChain(chain);
            rig.engine.setParameter(slotParam(i % kMaxBlocks, 0), gainParam(-6.0f));
        }
    });
    for (int i = 0; i < 4000; ++i)
        rig.run(1);
    running = false;
    editor.join();

    rig.run();
    CHECK(std::isfinite(rig.left()));
}
