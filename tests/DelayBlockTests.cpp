// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/blocks/DelayBlock.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace namsayin;

namespace {

constexpr double kRate = 48000.0;

const ParamSpec& spec(int param)
{
    return kDelayBlockDescriptor.params[param];
}

// A delay with clean repeats: no tone filter, no modulation, wet and dry at full level
std::unique_ptr<Block> cleanDelay(float timeMs, float feedback)
{
    std::unique_ptr<Block> block = testing::createWithDefaults(kDelayBlockDescriptor, kRate);
    block->setParam(kDelayParamTime, timeMs);
    block->setParam(kDelayParamFeedback, feedback);
    block->setParam(kDelayParamMix, 50.0f);
    block->setParam(kDelayParamTone, spec(kDelayParamTone).max);
    return block;
}

std::vector<std::vector<float>> impulse(size_t length, int channels, bool leftOnly = false)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length, 0.0f));
    for (int ch = 0; ch < channels; ++ch)
        x[ch][0] = (leftOnly && ch > 0) ? 0.0f : 1.0f;
    return x;
}

size_t peakAfter(const std::vector<float>& x, size_t from)
{
    size_t best = from;
    for (size_t i = from; i < x.size(); ++i)
        if (std::fabs(x[i]) > std::fabs(x[best]))
            best = i;
    return best;
}

} // namespace

TEST_CASE("delay repeats at the set time and doesn't allocate")
{
    std::unique_ptr<Block> block = cleanDelay(100.0f, 0.0f);
    auto x = impulse(16384, 1);
    int allocations = 0;
    testing::runBlock(*block, x, 128, &allocations);
    CHECK(allocations == 0);
    CHECK(x[0][0] == doctest::Approx(1.0f));
    CHECK(peakAfter(x[0], 100) == 4800);
    CHECK(std::fabs(x[0][4800]) > 0.9f);
    // no feedback: a single repeat
    CHECK(testing::rms(x[0], 5000, 16384) < 1e-3);
}

TEST_CASE("delay feedback makes decaying repeats")
{
    std::unique_ptr<Block> block = cleanDelay(50.0f, 50.0f);
    auto x = impulse(12000, 1);
    testing::runBlock(*block, x);
    CHECK(peakAfter(x[0], 3000) == 4800);
    CHECK(std::fabs(x[0][4800]) == doctest::Approx(0.5f).epsilon(0.1));
    CHECK(std::fabs(x[0][7200]) == doctest::Approx(0.25f).epsilon(0.1));
}

TEST_CASE("delay tempo sync follows the host tempo")
{
    std::unique_ptr<Block> block = cleanDelay(100.0f, 0.0f);
    int quarter = -1;
    for (int i = 0; i < kDelayNoteCount; ++i)
        if (std::strcmp(delayNoteName(i), "1/4") == 0)
            quarter = i;
    REQUIRE(quarter >= 0);
    const float normalized = static_cast<float>(kDelayNoteCount - 1 - quarter) / (kDelayNoteCount - 1);
    REQUIRE(delayNoteIndex(normalized) == quarter);

    block->setParam(kDelayParamSync, 1.0f);
    block->setParam(kDelayParamTime, spec(kDelayParamTime).toValue(normalized));
    block->setTempo(120.0); // a quarter note is 500 ms
    auto x = impulse(30000, 1);
    testing::runBlock(*block, x);
    CHECK(peakAfter(x[0], 100) == 24000);
}

TEST_CASE("delay stereo modes")
{
    SUBCASE("stereo spread offsets the right side")
    {
        std::unique_ptr<Block> block = cleanDelay(100.0f, 0.0f);
        block->setParam(kDelayParamMode, static_cast<float>(DelayMode::Stereo));
        block->setParam(kDelayParamSpread, 10.0f);
        auto x = impulse(8192, 2);
        testing::runBlock(*block, x);
        CHECK(peakAfter(x[0], 100) == 4800);
        CHECK(peakAfter(x[1], 100) == 5280);
    }

    SUBCASE("ping-pong bounces from left to right")
    {
        std::unique_ptr<Block> block = cleanDelay(100.0f, 50.0f);
        block->setParam(kDelayParamMode, static_cast<float>(DelayMode::PingPong));
        auto x = impulse(12000, 2, true);
        testing::runBlock(*block, x);
        // input enters as the mono sum (0.5); the left repeats at 1T, the right at 2T
        CHECK(peakAfter(x[0], 100) == 4800);
        CHECK(std::fabs(x[1][4800]) < 1e-3f);
        CHECK(peakAfter(x[1], 100) == 9600);
        CHECK(std::fabs(x[1][9600]) == doctest::Approx(0.25f).epsilon(0.1));
    }
}

TEST_CASE("delay stays bounded at full feedback")
{
    std::unique_ptr<Block> block = cleanDelay(20.0f, 100.0f);
    std::vector<std::vector<float>> x(1, std::vector<float>(kRate * 5));
    uint32_t seed = 1;
    for (float& v : x[0])
    {
        seed = seed * 1664525u + 1013904223u;
        v = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f);
    }
    testing::runBlock(*block, x);
    float peak = 0.0f;
    for (float v : x[0])
        peak = std::max(peak, std::fabs(v));
    CHECK(std::isfinite(peak));
    CHECK(peak < 4.0f);
}
