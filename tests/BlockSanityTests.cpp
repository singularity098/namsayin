// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/BlockRegistry.hpp"

#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace namsayin;

namespace {

// Noise bursts with gaps (so gates, envelopes and tails all get exercised)
std::vector<std::vector<float>> bursts(size_t length, int channels, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    std::vector<std::vector<float>> x(channels, std::vector<float>(length, 0.0f));
    for (int ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < length; ++i)
            x[ch][i] = (i / 6000) % 2 ? 0.0f : dist(rng);
    return x;
}

struct Result {
    bool finite = true;
    float peak = 0.0f;
    int allocations = 0;
};

Result run(const BlockDescriptor& type, const std::vector<float>& normalized, double rate, int channels,
           uint32_t seed)
{
    std::unique_ptr<Block> block = type.create();
    block->prepare(rate, 256);
    block->configure(nlohmann::json::object());
    for (int p = 0; p < type.numParams; ++p)
        block->setParam(p, type.params[p].toValue(normalized[p]));

    auto x = bursts(static_cast<size_t>(rate * 0.6), channels, seed);
    std::vector<float> rig = x[0];
    const float* rigPtr[2] = { rig.data(), rig.data() };
    Result result;
    const size_t length = x[0].size();
    for (size_t t = 0; t < length; t += 200)
    {
        const auto n = static_cast<uint32_t>(std::min<size_t>(200, length - t));
        float* io[2] = { x[0].data() + t, x[channels > 1 ? 1 : 0].data() + t };
        const float* key[2] = { rigPtr[0] + t, rigPtr[1] + t };
        testing::AllocationGuard guard;
        block->setTempo(97.0);
        block->setRigInput(key, static_cast<uint32_t>(channels));
        block->process(io, static_cast<uint32_t>(channels), n);
        result.allocations += guard.count();
    }
    for (const auto& ch : x)
    {
        for (float v : ch)
        {
            if (!std::isfinite(v))
                result.finite = false;
            result.peak = std::max(result.peak, std::fabs(v));
        }
    }
    return result;
}

} // namespace

TEST_CASE("every built-in block stays finite and bounded, and doesn't allocate, at any settings")
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        std::vector<std::vector<float>> sets;
        std::vector<float> defaults, low, high;
        for (int p = 0; p < type->numParams; ++p)
        {
            defaults.push_back(type->params[p].toNormalized(type->params[p].def));
            low.push_back(0.0f);
            high.push_back(1.0f);
        }
        sets.push_back(defaults);
        sets.push_back(low);
        sets.push_back(high);
        for (int k = 0; k < 12; ++k)
        {
            std::vector<float> set;
            for (int p = 0; p < type->numParams; ++p)
                set.push_back(unit(rng));
            sets.push_back(set);
        }

        for (size_t s = 0; s < sets.size(); ++s)
        {
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
            {
                for (const int channels : { 1, 2 })
                {
            const std::string typeId = type->typeId;
            CAPTURE(typeId);
                    CAPTURE(s);
                    CAPTURE(rate);
                    CAPTURE(channels);
                    const Result r = run(*type, sets[s], rate, channels, static_cast<uint32_t>(s + 1));
                    CHECK(r.finite);
                    CHECK(r.allocations == 0);
                    // input peaks at 0.5; nothing should come out wildly louder (some drives and boosts add
                    // gain, but not this much)
                    CHECK(r.peak < 40.0f);
                }
            }
        }
    }
}

TEST_CASE("every built-in block is silent for silence at its defaults")
{
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
            const std::string typeId = type->typeId;
            CAPTURE(typeId);
        std::unique_ptr<Block> block = testing::createWithDefaults(*type, 48000.0, 256);
        std::vector<std::vector<float>> x(2, std::vector<float>(48000, 0.0f));
        testing::runBlock(*block, x, 256);
        float peak = 0.0f;
        for (const auto& ch : x)
            for (float v : ch)
                peak = std::max(peak, std::fabs(v));
        CHECK(peak < 1e-4f);
    }
}

TEST_CASE("block descriptors are consistent")
{
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
            const std::string typeId = type->typeId;
            CAPTURE(typeId);
        CHECK(type->numParams <= kParamsPerSlot);
        CHECK(BlockRegistry::builtin().find(type->typeId) == type);
        for (int p = 0; p < type->numParams; ++p)
        {
            const ParamSpec& spec = type->params[p];
            CAPTURE(spec.name);
            CHECK(spec.def >= spec.min);
            CHECK(spec.def <= spec.max);
            if (spec.scale == ParamScale::Choice)
                CHECK(spec.choices != nullptr);
            if (spec.scale == ParamScale::Log)
                CHECK(spec.min > 0.0f);
        }
        if (type->syncParam >= 0)
        {
            CHECK(type->params[type->syncParam].scale == ParamScale::Toggle);
            CHECK(type->syncedParam >= 0);
        }
    }
}

TEST_CASE("no built-in block runs away over a long stretch")
{
    // 3 s of bursts, then silence: the last second (9-10 s) must not be louder than the loudest second before it
    // up to 6 s (feedback loops at their limits may hold, but never grow once the input stops)
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const double rate = 48000.0;
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        std::vector<std::vector<float>> sets;
        std::vector<float> high, random;
        for (int p = 0; p < type->numParams; ++p)
        {
            high.push_back(1.0f);
            random.push_back(unit(rng));
        }
        sets.push_back(high);
        sets.push_back(random);
        for (size_t s = 0; s < sets.size(); ++s)
        {
            const std::string typeId = type->typeId;
            CAPTURE(typeId);
            CAPTURE(s);
            std::unique_ptr<Block> block = type->create();
            block->prepare(rate, 256);
            block->configure(nlohmann::json::object());
            for (int p = 0; p < type->numParams; ++p)
                block->setParam(p, type->params[p].toValue(sets[s][p]));
            std::vector<std::vector<float>> x = bursts(static_cast<size_t>(rate * 10), 1, 7);
            std::fill(x[0].begin() + static_cast<long>(rate * 3), x[0].end(), 0.0f);
            testing::runBlock(*block, x, 256);
            double loudest = 0.0;
            for (size_t second = 0; second < 6; ++second)
                loudest = std::max(loudest, testing::rms(x[0], second * 48000, (second + 1) * 48000));
            const double last = testing::rms(x[0], 9 * 48000, 10 * 48000);
            CHECK(std::isfinite(last));
            CHECK(last <= loudest * 1.0001 + 1e-9);
        }
    }
}
