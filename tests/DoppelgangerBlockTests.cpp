// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/blocks/DoppelgangerBlock.hpp"

#include <cmath>
#include <random>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;
constexpr size_t kEcho = 2400; // 50 ms

const ParamSpec& spec(int param)
{
    return kDoppelgangerBlockDescriptor.params[param];
}

// 50 ms echoes at 50% feedback, no tone filter, wet and dry at full level, both effects out of the way and fixed
// (zero depth) so each one's output is predictable: the flanger adds a copy 0.25 ms (12 samples) later, a
// one-voice chorus a copy 10 ms (480 samples) later
std::unique_ptr<Block> testDoppelganger(float flangeMix, float chorusMix)
{
    std::unique_ptr<Block> block = testing::createWithDefaults(kDoppelgangerBlockDescriptor, kRate);
    block->setParam(kDoppelParamTime, 50.0f);
    block->setParam(kDoppelParamFeedback, 50.0f);
    block->setParam(kDoppelParamMix, 50.0f);
    block->setParam(kDoppelParamTone, spec(kDoppelParamTone).max);
    block->setParam(kDoppelParamFlangeDepth, 0.0f);
    block->setParam(kDoppelParamFlangeRegen, 0.0f);
    block->setParam(kDoppelParamFlangeMix, flangeMix);
    block->setParam(kDoppelParamChorusDepth, 0.0f);
    block->setParam(kDoppelParamChorusVoices, 1.0f);
    block->setParam(kDoppelParamChorusMix, chorusMix);
    block->setParam(kDoppelParamBounce, 0.0f);
    return block;
}

std::vector<std::vector<float>> impulse(size_t length, int channels)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length, 0.0f));
    for (int ch = 0; ch < channels; ++ch)
        x[ch][0] = 1.0f;
    return x;
}

constexpr size_t kFlangeOffset = 12;
constexpr size_t kChorusOffset = 480;

} // namespace

TEST_CASE("doppelganger with both effects off is a plain delay, and doesn't allocate")
{
    std::unique_ptr<Block> block = testDoppelganger(0.0f, 0.0f);
    auto x = impulse(4 * kEcho + 100, 1);
    int allocations = 0;
    testing::runBlock(*block, x, 128, &allocations);
    CHECK(allocations == 0);
    CHECK(x[0][0] == doctest::Approx(1.0f));
    CHECK(x[0][kEcho] == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(x[0][2 * kEcho] == doctest::Approx(0.25f).epsilon(0.05));
    CHECK(x[0][3 * kEcho] == doctest::Approx(0.125f).epsilon(0.05));
    CHECK(x[0][4 * kEcho] == doctest::Approx(0.0625f).epsilon(0.05));
}

TEST_CASE("doppelganger flanges the odd echoes and only those")
{
    std::unique_ptr<Block> block = testDoppelganger(100.0f, 0.0f);
    auto x = impulse(4 * kEcho + 100, 1);
    testing::runBlock(*block, x);
    // echoes 1 and 3: half the echo, then the flanger's copy
    CHECK(x[0][kEcho] == doctest::Approx(0.25f).epsilon(0.05));
    CHECK(x[0][kEcho + kFlangeOffset] == doctest::Approx(0.25f).epsilon(0.05));
    CHECK(x[0][3 * kEcho + kFlangeOffset] == doctest::Approx(0.0625f).epsilon(0.05));
    // echoes 2 and 4: clean
    CHECK(x[0][2 * kEcho] == doctest::Approx(0.25f).epsilon(0.05));
    CHECK(std::fabs(x[0][2 * kEcho + kFlangeOffset]) < 5e-3f);
    CHECK(x[0][4 * kEcho] == doctest::Approx(0.0625f).epsilon(0.05));
    CHECK(std::fabs(x[0][4 * kEcho + kFlangeOffset]) < 5e-3f);
}

TEST_CASE("doppelganger choruses the even echoes and only those")
{
    std::unique_ptr<Block> block = testDoppelganger(0.0f, 100.0f);
    auto x = impulse(4 * kEcho + 600, 1);
    testing::runBlock(*block, x);
    // echoes 1 and 3: clean
    CHECK(x[0][kEcho] == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(std::fabs(x[0][kEcho + kChorusOffset]) < 5e-3f);
    CHECK(std::fabs(x[0][3 * kEcho + kChorusOffset]) < 5e-3f);
    // echoes 2 and 4: the echo and the chorus voice
    CHECK(x[0][2 * kEcho] == doctest::Approx(0.7f * 0.25f).epsilon(0.05));
    CHECK(x[0][2 * kEcho + kChorusOffset] == doctest::Approx(0.7f * 0.25f).epsilon(0.05));
    CHECK(x[0][4 * kEcho + kChorusOffset] == doctest::Approx(0.7f * 0.0625f).epsilon(0.05));
}

TEST_CASE("doppelganger order swaps the effects")
{
    std::unique_ptr<Block> block = testDoppelganger(100.0f, 100.0f);
    block->setParam(kDoppelParamOrder, static_cast<float>(DoppelgangerOrder::ChorusFirst));
    auto x = impulse(3 * kEcho + 600, 1);
    testing::runBlock(*block, x);
    CHECK(x[0][kEcho + kChorusOffset] == doctest::Approx(0.7f * 0.5f).epsilon(0.05));
    CHECK(std::fabs(x[0][kEcho + kFlangeOffset]) < 5e-3f);
    CHECK(x[0][2 * kEcho + kFlangeOffset] == doctest::Approx(0.125f).epsilon(0.05));
    CHECK(std::fabs(x[0][2 * kEcho + kChorusOffset]) < 5e-3f);
}

TEST_CASE("doppelganger bounce sends flanged echoes left and chorused echoes right")
{
    std::unique_ptr<Block> block = testDoppelganger(0.0f, 0.0f);
    block->setParam(kDoppelParamBounce, 100.0f);
    auto x = impulse(2 * kEcho + 100, 2);
    testing::runBlock(*block, x);
    CHECK(x[0][kEcho] == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(std::fabs(x[1][kEcho]) < 5e-3f);
    CHECK(std::fabs(x[0][2 * kEcho]) < 5e-3f);
    CHECK(x[1][2 * kEcho] == doctest::Approx(0.25f).epsilon(0.05));
}

TEST_CASE("doppelganger stays bounded at full feedback and regen")
{
    std::unique_ptr<Block> block = testing::createWithDefaults(kDoppelgangerBlockDescriptor, kRate);
    block->setParam(kDoppelParamTime, 30.0f);
    block->setParam(kDoppelParamFeedback, 100.0f);
    block->setParam(kDoppelParamTone, spec(kDoppelParamTone).max);
    block->setParam(kDoppelParamFlangeRegen, spec(kDoppelParamFlangeRegen).max);
    block->setParam(kDoppelParamFlangeDepth, 100.0f);
    block->setParam(kDoppelParamChorusDepth, 100.0f);
    block->setParam(kDoppelParamFlangeRate, 5.0f);
    block->setParam(kDoppelParamWidth, 100.0f);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<std::vector<float>> x(2, std::vector<float>(5 * 48000));
    for (auto& channel : x)
        for (float& v : channel)
            v = dist(rng);
    int allocations = 0;
    testing::runBlock(*block, x, 256, &allocations);
    CHECK(allocations == 0);
    float peak = 0.0f;
    bool finite = true;
    for (const auto& channel : x)
        for (const float v : channel)
        {
            finite = finite && std::isfinite(v);
            peak = std::fmax(peak, std::fabs(v));
        }
    CHECK(finite);
    CHECK(peak < 8.0f);
}
