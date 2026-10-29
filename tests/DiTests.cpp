// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// Behaviour tests for the DI category: the bass DI boxes and preamps (blocks/DiBlocks.*)

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/BlockRegistry.hpp"
#include "engine/blocks/DiBlocks.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;
constexpr size_t kLength = 48000;

std::vector<std::vector<float>> sine(double hz, float amplitude)
{
    std::vector<std::vector<float>> x(1, std::vector<float>(kLength));
    for (size_t i = 0; i < kLength; ++i)
        x[0][i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kRate));
    return x;
}

// Amplitude of one frequency over the second half (a single-bin DFT with a Hann window)
double toneAt(const std::vector<float>& x, double hz)
{
    const size_t from = kLength / 2, to = kLength;
    double re = 0.0, im = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i - from) / n);
        const double angle = 2.0 * M_PI * hz * static_cast<double>(i) / kRate;
        re += w * x[i] * std::cos(angle);
        im -= w * x[i] * std::sin(angle);
    }
    return 4.0 * std::sqrt(re * re + im * im) / n;
}

struct Response {
    double gainDb;  // the fundamental, out against in
    double thirdDb; // the 3rd harmonic against the fundamental
    double secondDb;
};

Response respond(Block& block, double hz, float amplitude)
{
    auto x = sine(hz, amplitude);
    block.reset();
    testing::runBlock(block, x);
    const double fundamental = toneAt(x[0], hz);
    return { 20.0 * std::log10(fundamental / amplitude), 20.0 * std::log10(toneAt(x[0], 3.0 * hz) / fundamental + 1e-12),
             20.0 * std::log10(toneAt(x[0], 2.0 * hz) / fundamental + 1e-12) };
}

std::unique_ptr<Block> make(const BlockDescriptor& type)
{
    return testing::createWithDefaults(type, kRate);
}

} // namespace

TEST_CASE("the DI boxes are in their own category and near unity gain at their defaults")
{
    int count = 0;
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        if (type->category != Category::Di)
            continue;
        ++count;
        const std::string id = type->typeId;
        CAPTURE(id);
        auto block = make(*type);
        // a bass DI's level: -20 dBFS peaks
        for (const double hz : { 82.0, 330.0, 1000.0 })
        {
            CAPTURE(hz);
            const Response r = respond(*block, hz, 0.1f);
            CHECK(r.gainDb > -10.0);
            CHECK(r.gainDb < 10.0);
        }
    }
    CHECK(count == 10);
    CHECK(std::string(categoryName(Category::Di)) == "DI");
}

TEST_CASE("studio DI: clean and flat without Iron, Iron saturates the lows and not the highs")
{
    auto di = make(kStudioDiBlockDescriptor);
    di->setParam(kStudioDiIron, 0.0f);
    for (const double hz : { 60.0, 500.0, 5000.0 })
    {
        CAPTURE(hz);
        const Response r = respond(*di, hz, 0.3f);
        CHECK(std::fabs(r.gainDb) < 0.5);
        CHECK(r.thirdDb < -60.0);
    }
    di->setParam(kStudioDiIron, 100.0f);
    const Response low = respond(*di, 50.0, 0.5f);
    const Response high = respond(*di, 2000.0, 0.5f);
    CHECK(low.thirdDb > -30.0);  // the core saturates on the lows
    CHECK(low.secondDb > -60.0); // with a little asymmetry
    CHECK(high.thirdDb < -50.0); // the highs stay clean
}

TEST_CASE("studio DI: Low Cut takes out the lowest lows")
{
    auto di = make(kStudioDiBlockDescriptor);
    di->setParam(kStudioDiIron, 0.0f);
    di->setParam(kStudioDiLowCut, 3.0f); // 100 Hz
    CHECK(respond(*di, 40.0, 0.1f).gainDb < -10.0);
    CHECK(respond(*di, 1000.0, 0.1f).gainDb > -0.5);
}

TEST_CASE("crossover drive keeps the lows clean and drives the highs")
{
    auto x = make(kCrossoverDriveBlockDescriptor);
    x->setParam(kXoverFreq, 300.0f);
    x->setParam(kXoverDrive, 100.0f);
    const Response low = respond(*x, 50.0, 0.3f);
    const Response high = respond(*x, 1500.0, 0.3f);
    CHECK(low.thirdDb < -35.0);
    CHECK(high.thirdDb > -25.0);
    CHECK(std::fabs(low.gainDb) < 1.0); // Lows at 0 dB
}

TEST_CASE("crossover drive's bands sum back flat")
{
    // with the drive lowest, a quiet signal (where the drive stays linear) comes through about flat, at the
    // crossover too; with the highs turned down, only the lows are left
    auto x = make(kCrossoverDriveBlockDescriptor);
    x->setParam(kXoverDrive, 0.0f);
    x->setParam(kXoverTone, 100.0f);
    for (const double hz : { 50.0, 220.0, 1000.0 })
    {
        CAPTURE(hz);
        CHECK(std::fabs(respond(*x, hz, 0.02f).gainDb) < 1.5);
    }
    x->setParam(kXoverHighs, -24.0f);
    CHECK(respond(*x, 50.0, 0.02f).gainDb > -0.5);
    CHECK(respond(*x, 2000.0, 0.02f).gainDb < -20.0);
}

TEST_CASE("blend at 0 is the clean bass")
{
    auto driver = make(kDriverDiBlockDescriptor);
    driver->setParam(kDriverBlend, 0.0f);
    driver->setParam(kDriverDrive, 100.0f);
    auto modern = make(kModernDriveBlockDescriptor);
    modern->setParam(kModernBlend, 0.0f);
    modern->setParam(kModernDrive, 100.0f);
    for (Block* block : { driver.get(), modern.get() })
        for (const double hz : { 80.0, 1000.0, 4000.0 })
        {
            CAPTURE(hz);
            const Response r = respond(*block, hz, 0.3f);
            CHECK(std::fabs(r.gainDb) < 0.5);
            CHECK(r.thirdDb < -60.0);
        }
}

TEST_CASE("driven boxes distort when driven")
{
    struct Case {
        const BlockDescriptor* type;
        int drive;
    };
    for (const Case c : { Case { &kDriverDiBlockDescriptor, kDriverDrive }, Case { &kModernDriveBlockDescriptor, kModernDrive },
                          Case { &kDualDriveBlockDescriptor, kDualDrive }, Case { &kTubePreampBlockDescriptor, kTubePreGain } })
    {
        CAPTURE(c.type->typeId);
        auto block = make(*c.type);
        block->setParam(c.drive, 0.0f);
        const double clean = respond(*block, 220.0, 0.2f).thirdDb;
        block->setParam(c.drive, 100.0f);
        const double driven = respond(*block, 220.0, 0.2f).thirdDb;
        CHECK(driven > clean + 6.0);
    }
}

TEST_CASE("tube preamp: Ultra Hi lifts the top, Ultra Lo scoops the mids")
{
    auto pre = make(kTubePreampBlockDescriptor);
    pre->setParam(kTubePreGain, 0.0f);
    const double top = respond(*pre, 9000.0, 0.02f).gainDb;
    const double mid = respond(*pre, 500.0, 0.02f).gainDb;
    pre->setParam(kTubePreUltraHi, 1.0f);
    CHECK(respond(*pre, 9000.0, 0.02f).gainDb > top + 4.0);
    pre->setParam(kTubePreUltraLo, 1.0f);
    CHECK(respond(*pre, 500.0, 0.02f).gainDb < mid - 7.0);
}

TEST_CASE("british preamp: Pre Shape scoops, the graphic's bands boost where they say")
{
    auto pre = make(kBritishPreampBlockDescriptor);
    pre->setParam(kBritGain, 0.0f);
    const double mid = respond(*pre, 400.0, 0.05f).gainDb;
    pre->setParam(kBritPreShape, 1.0f);
    CHECK(respond(*pre, 400.0, 0.05f).gainDb < mid - 7.0);
    pre->setParam(kBritPreShape, 0.0f);
    const double at600 = respond(*pre, 600.0, 0.05f).gainDb;
    pre->setParam(kBrit600, 12.0f);
    CHECK(respond(*pre, 600.0, 0.05f).gainDb > at600 + 10.0);
}

TEST_CASE("dual drive mixes its channels and Color scoops the distortion")
{
    auto dual = make(kDualDriveBlockDescriptor);
    dual->setParam(kDualClean, 0.0f);
    dual->setParam(kDualDist, -30.0f);
    const Response clean = respond(*dual, 220.0, 0.2f);
    CHECK(std::fabs(clean.gainDb) < 1.0);
    dual->setParam(kDualClean, -30.0f);
    dual->setParam(kDualDist, 0.0f);
    const double mid = respond(*dual, 500.0, 0.01f).gainDb;
    dual->setParam(kDualColor, 1.0f);
    CHECK(respond(*dual, 500.0, 0.01f).gainDb < mid - 4.0);
}

TEST_CASE("valve DI's voicings shape the tone")
{
    auto valve = make(kValveDiBlockDescriptor);
    valve->setParam(kValveDiWarmth, 0.0f);
    const double flatLow = respond(*valve, 50.0, 0.05f).gainDb;
    const double flatTop = respond(*valve, 8000.0, 0.05f).gainDb;
    CHECK(std::fabs(flatLow) < 1.0);
    valve->setParam(kValveDiCharacter, 1.0f); // Deep
    CHECK(respond(*valve, 50.0, 0.05f).gainDb > flatLow + 3.0);
    valve->setParam(kValveDiCharacter, 5.0f); // Vintage
    CHECK(respond(*valve, 8000.0, 0.05f).gainDb < flatTop - 6.0);
}

TEST_CASE("opto DI turns loud playing down and shows it, and leaves quiet playing alone")
{
    auto opto = make(kOptoDiBlockDescriptor);
    opto->setParam(kOptoPeakReduction, 80.0f);
    const Response loud = respond(*opto, 110.0, 0.5f);
    CHECK(opto->meter() > 6.0f);
    CHECK(loud.gainDb < -6.0);
    const Response quiet = respond(*opto, 110.0, 0.002f);
    CHECK(opto->meter() < 0.5f);
    CHECK(std::fabs(quiet.gainDb) < 0.5);
    // Mix at 0: the dry signal
    opto->setParam(kOptoMix, 0.0f);
    CHECK(std::fabs(respond(*opto, 110.0, 0.5f).gainDb) < 1.5); // (the output stage rounds off the peaks a little)
}

TEST_CASE("hi-fi preamp's compressor is off at 0 and evens out loud playing")
{
    auto hifi = make(kHiFiPreampBlockDescriptor);
    hifi->setParam(kHiFiGain, 0.0f);
    hifi->setParam(kHiFiComp, 0.0f);
    respond(*hifi, 110.0, 0.5f);
    CHECK(hifi->meter() == 0.0f);
    hifi->setParam(kHiFiComp, 100.0f);
    const double loud = respond(*hifi, 110.0, 0.5f).gainDb;
    const double quiet = respond(*hifi, 110.0, 0.005f).gainDb;
    CHECK(hifi->meter() < 1.0f);
    CHECK(quiet - loud > 10.0);
    // the mid sweeps
    hifi->setParam(kHiFiComp, 0.0f);
    hifi->setParam(kHiFiMid, 12.0f);
    hifi->setParam(kHiFiMidFreq, 200.0f);
    const double at200 = respond(*hifi, 200.0, 0.02f).gainDb;
    const double at900 = respond(*hifi, 900.0, 0.02f).gainDb;
    CHECK(at200 > at900 + 6.0);
}


// The boxes' levels and 3rd harmonic at their defaults, for a -20 dBFS bass: XYTHOS_DI_LEVELS=1 ./build/xythos-tests -tc="DI levels*"
TEST_CASE("DI levels at the defaults" * doctest::skip(std::getenv("XYTHOS_DI_LEVELS") == nullptr))
{
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        if (type->category != Category::Di)
            continue;
        auto block = make(*type);
        std::printf("%-16s", type->typeId);
        for (const double hz : { 41.0, 82.0, 330.0, 1000.0, 3000.0, 8000.0 })
        {
            const Response r = respond(*block, hz, 0.1f);
            std::printf(" %5.0f: %+5.1f dB (h3 %+4.0f)", hz, r.gainDb, r.thirdDb);
        }
        std::printf("\n");
    }
}
