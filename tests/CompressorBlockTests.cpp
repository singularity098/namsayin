// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/blocks/CompressorBlock.hpp"

#include <cmath>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<Block> compressor(float threshold, float ratio, float knee = 0.0f)
{
    std::unique_ptr<Block> block = testing::createWithDefaults(kCompressorBlockDescriptor, kRate);
    block->setParam(kCompParamThreshold, threshold);
    block->setParam(kCompParamRatio, ratio);
    block->setParam(kCompParamKnee, knee);
    block->setParam(kCompParamAttack, 1.0f);
    block->setParam(kCompParamRelease, 50.0f);
    return block;
}

// A square wave keeps the peak detector steady, so the steady-state gain is exact
std::vector<std::vector<float>> square(float amplitude, size_t length, int channels = 1)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length));
    for (auto& ch : x)
        for (size_t i = 0; i < length; ++i)
            ch[i] = (i / 48) % 2 ? amplitude : -amplitude;
    return x;
}

double levelDb(const std::vector<float>& x, size_t from, size_t to)
{
    return 20.0 * std::log10(testing::rms(x, from, to));
}

} // namespace

TEST_CASE("compressor static curve")
{
    // below threshold: unchanged; above: threshold + excess / ratio
    CHECK(compressorCurve(-30.0f, -20.0f, 4.0f, 0.0f) == doctest::Approx(-30.0f));
    CHECK(compressorCurve(-10.0f, -20.0f, 4.0f, 0.0f) == doctest::Approx(-17.5f));
    CHECK(compressorCurve(0.0f, -20.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));
    // the knee is continuous at both ends and passes through threshold + knee curve midpoint
    CHECK(compressorCurve(-23.0f, -20.0f, 4.0f, 6.0f) == doctest::Approx(-23.0f));
    CHECK(compressorCurve(-17.0f, -20.0f, 4.0f, 6.0f) == doctest::Approx(-20.0f + 3.0f / 4.0f));
    CHECK(compressorCurve(-20.0f, -20.0f, 4.0f, 6.0f) < -20.0f);
}

TEST_CASE("compressor reduces gain by the expected amount and reports it, without allocating")
{
    std::unique_ptr<Block> block = compressor(-20.0f, 4.0f);
    auto x = square(std::pow(10.0f, -10.0f / 20.0f), static_cast<size_t>(kRate));
    int allocations = 0;
    testing::runBlock(*block, x, 128, &allocations);
    CHECK(allocations == 0);
    // -10 dB in, threshold -20, ratio 4: -17.5 dB out, 7.5 dB of reduction
    CHECK(levelDb(x[0], x[0].size() / 2, x[0].size()) == doctest::Approx(-17.5).epsilon(0.01));
    CHECK(block->meter() == doctest::Approx(7.5f).epsilon(0.01));
}

TEST_CASE("compressor leaves quiet signals alone; makeup and mix")
{
    std::unique_ptr<Block> block = compressor(-20.0f, 4.0f);
    auto quiet = square(std::pow(10.0f, -30.0f / 20.0f), 24000);
    testing::runBlock(*block, quiet);
    CHECK(levelDb(quiet[0], 12000, 24000) == doctest::Approx(-30.0).epsilon(0.01));
    CHECK(block->meter() == doctest::Approx(0.0f));

    block->setParam(kCompParamMakeup, 6.0f);
    auto loud = square(std::pow(10.0f, -10.0f / 20.0f), 48000);
    testing::runBlock(*block, loud);
    CHECK(levelDb(loud[0], 24000, 48000) == doctest::Approx(-11.5).epsilon(0.01));

    // fully dry: untouched
    block->setParam(kCompParamMix, 0.0f);
    auto dry = square(std::pow(10.0f, -10.0f / 20.0f), 48000);
    testing::runBlock(*block, dry);
    CHECK(levelDb(dry[0], 24000, 48000) == doctest::Approx(-10.0).epsilon(0.01));
}

TEST_CASE("compressor links the stereo channels")
{
    std::unique_ptr<Block> block = compressor(-20.0f, 4.0f);
    auto x = square(std::pow(10.0f, -10.0f / 20.0f), 48000, 2);
    for (float& v : x[1])
        v *= 0.01f; // the quiet side gets the loud side's gain reduction
    testing::runBlock(*block, x);
    CHECK(levelDb(x[1], 24000, 48000) == doctest::Approx(-50.0 - 7.5).epsilon(0.01));
}

TEST_CASE("compressor attack and release")
{
    std::unique_ptr<Block> block = compressor(-20.0f, 10.0f);
    block->setParam(kCompParamAttack, 10.0f);
    block->setParam(kCompParamRelease, 100.0f);
    // loud for 200 ms then quiet: reduction builds over ~10 ms and recovers over ~100 ms
    auto x = square(1.0f, 48000);
    for (size_t i = 9600; i < x[0].size(); ++i)
        x[0][i] *= 0.001f;

    std::vector<float> meters;
    for (size_t t = 0; t < x[0].size(); t += 48)
    {
        std::vector<std::vector<float>> chunk(1, std::vector<float>(x[0].begin() + t, x[0].begin() + t + 48));
        testing::runBlock(*block, chunk, 48);
        meters.push_back(block->meter());
    }
    // full reduction: 20 dB over the threshold at 10:1 = 18 dB
    CHECK(meters[2] < 18.0f * 0.5f);       // after 2 ms: still building
    CHECK(meters[150] == doctest::Approx(18.0f).epsilon(0.02)); // after 150 ms: settled
    CHECK(meters[200 + 20] > 18.0f * 0.5f); // 20 ms into the release: mostly still reduced
    CHECK(meters[200 + 600] < 0.5f);     // 600 ms into the release: recovered
}
