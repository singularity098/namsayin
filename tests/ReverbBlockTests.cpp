// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/blocks/ReverbBlock.hpp"

#include <cmath>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<Block> reverb(ReverbType type, float decaySeconds, double rate = kRate)
{
    std::unique_ptr<Block> block = testing::createWithDefaults(kReverbBlockDescriptor, rate);
    block->setParam(kReverbParamType, static_cast<float>(type));
    block->setParam(kReverbParamDecay, decaySeconds);
    block->setParam(kReverbParamPreDelay, 0.0f);
    block->setParam(kReverbParamLowCut, 20.0f);
    block->setParam(kReverbParamHighCut, 20000.0f);
    block->setParam(kReverbParamMix, 100.0f); // wet only
    return block;
}

std::vector<std::vector<float>> impulse(size_t length, int channels)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length, 0.0f));
    for (auto& ch : x)
        ch[0] = 1.0f;
    return x;
}

double db(double ratio)
{
    return 20.0 * std::log10(ratio);
}

} // namespace

TEST_CASE("reverb produces a decaying stereo tail for every type, without allocating")
{
    for (ReverbType type : { ReverbType::Room, ReverbType::Hall, ReverbType::Plate })
    {
        CAPTURE(static_cast<int>(type));
        std::unique_ptr<Block> block = reverb(type, 2.0f);
        auto x = impulse(static_cast<size_t>(kRate * 3), 2);
        int allocations = 0;
        testing::runBlock(*block, x, 128, &allocations);
        CHECK(allocations == 0);

        const auto window = static_cast<size_t>(kRate * 0.1);
        const double early = testing::rms(x[0], window, 2 * window);
        const double late = testing::rms(x[0], 15 * window, 16 * window);
        CHECK(early > 1e-4);
        CHECK(late < early);

        // the two sides are different (decorrelated), not a copy
        double diff = 0.0;
        for (size_t i = 0; i < x[0].size(); ++i)
            diff += std::fabs(x[0][i] - x[1][i]);
        CHECK(diff > 1.0);
    }
}

TEST_CASE("reverb decay time is roughly what is set")
{
    // a 2 s RT60 drops 30 dB per second; allow a wide margin for the diffuse build-up and modulation
    for (ReverbType type : { ReverbType::Hall, ReverbType::Plate })
    {
        CAPTURE(static_cast<int>(type));
        std::unique_ptr<Block> block = reverb(type, 2.0f);
        block->setParam(kReverbParamDamping, 0.0f);
        auto x = impulse(static_cast<size_t>(kRate * 2.5), 1);
        testing::runBlock(*block, x);
        const auto window = static_cast<size_t>(kRate * 0.1);
        const double at05 = testing::rms(x[0], 5 * window, 6 * window);
        const double at15 = testing::rms(x[0], 15 * window, 16 * window);
        const double dropPerSecond = db(at05 / at15);
        CHECK(dropPerSecond > 20.0);
        CHECK(dropPerSecond < 40.0);
    }

    // longer decay, more tail
    std::unique_ptr<Block> shortTail = reverb(ReverbType::Hall, 0.5f);
    std::unique_ptr<Block> longTail = reverb(ReverbType::Hall, 8.0f);
    auto a = impulse(static_cast<size_t>(kRate * 2), 1);
    auto b = a;
    testing::runBlock(*shortTail, a);
    testing::runBlock(*longTail, b);
    const auto from = static_cast<size_t>(kRate), to = static_cast<size_t>(kRate * 2);
    CHECK(testing::rms(b[0], from, to) > 100.0 * testing::rms(a[0], from, to));
}

TEST_CASE("reverb mix at 0 passes the dry signal")
{
    std::unique_ptr<Block> block = reverb(ReverbType::Plate, 3.0f);
    block->setParam(kReverbParamMix, 0.0f);
    auto x = impulse(8192, 2);
    testing::runBlock(*block, x);
    CHECK(x[0][0] == doctest::Approx(1.0f));
    CHECK(testing::rms(x[0], 1, 8192) < 1e-6);
}

TEST_CASE("reverb pre-delay holds the tail back")
{
    std::unique_ptr<Block> block = reverb(ReverbType::Plate, 2.0f);
    block->setParam(kReverbParamPreDelay, 100.0f);
    auto x = impulse(static_cast<size_t>(kRate * 0.3), 1);
    testing::runBlock(*block, x);
    CHECK(testing::rms(x[0], 0, static_cast<size_t>(kRate * 0.099)) < 1e-6);
    CHECK(testing::rms(x[0], static_cast<size_t>(kRate * 0.1), static_cast<size_t>(kRate * 0.3)) > 1e-4);
}

TEST_CASE("reverb stays stable at the longest decay and at 96 kHz")
{
    for (double rate : { 44100.0, 96000.0 })
    {
        CAPTURE(rate);
        std::unique_ptr<Block> block = reverb(ReverbType::Hall, 20.0f, rate);
        block->setParam(kReverbParamDamping, 0.0f);
        std::vector<std::vector<float>> x(2, std::vector<float>(static_cast<size_t>(rate * 10)));
        uint32_t seed = 7;
        for (size_t i = 0; i < x[0].size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            x[0][i] = x[1][i] = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.5f;
        }
        testing::runBlock(*block, x);
        float peak = 0.0f;
        for (const auto& ch : x)
            for (float v : ch)
                peak = std::max(peak, std::fabs(v));
        CHECK(std::isfinite(peak));
        CHECK(peak < 10.0f);
    }
}
