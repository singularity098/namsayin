// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// Behaviour tests for the compressor pedals (blocks/CompBlocks.*)

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/BlockRegistry.hpp"
#include "engine/blocks/CompBlocks.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;
constexpr size_t kLength = 48000;

const BlockDescriptor* const kPedals[] = { &kDiodeBridgeBlockDescriptor, &kConsoleCompBlockDescriptor,
                                           &kOptoLevelerBlockDescriptor, &kOptoStompBlockDescriptor,
                                           &kFetLimiterBlockDescriptor,  &kSquashCompBlockDescriptor };

std::vector<std::vector<float>> sine(double hz, float amplitude, size_t length = kLength)
{
    std::vector<std::vector<float>> x(1, std::vector<float>(length));
    for (size_t i = 0; i < length; ++i)
        x[0][i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kRate));
    return x;
}

// Amplitude of one frequency over the second half (a single-bin DFT with a Hann window)
double toneAt(const std::vector<float>& x, double hz)
{
    const size_t from = x.size() / 2, to = x.size();
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
    double gainDb;   // the fundamental, out against in, once settled
    double secondDb; // the 2nd and 3rd harmonics against the fundamental
    double thirdDb;
};

Response respond(Block& block, double hz, float amplitude)
{
    auto x = sine(hz, amplitude);
    block.reset();
    testing::runBlock(block, x);
    const double fundamental = toneAt(x[0], hz);
    return { 20.0 * std::log10(fundamental / amplitude), 20.0 * std::log10(toneAt(x[0], 2.0 * hz) / fundamental + 1e-12),
             20.0 * std::log10(toneAt(x[0], 3.0 * hz) / fundamental + 1e-12) };
}

std::unique_ptr<Block> make(const BlockDescriptor& type)
{
    return testing::createWithDefaults(type, kRate);
}

// The meter (gain reduction, dB) `afterMs` after a loud 220 Hz burst of `burstMs` stops
float reductionAfter(Block& block, double burstMs, double afterMs)
{
    const auto burst = static_cast<size_t>(burstMs * kRate / 1000.0);
    const auto after = static_cast<size_t>(afterMs * kRate / 1000.0);
    auto x = sine(220.0, 0.5f, burst + after);
    for (size_t i = burst; i < x[0].size(); ++i)
        x[0][i] = 0.0f;
    block.reset();
    testing::runBlock(block, x, 64);
    return block.meter();
}

} // namespace

TEST_CASE("the compressor pedals are dynamics blocks, near unity at their defaults, and turn loud playing down")
{
    for (const BlockDescriptor* type : kPedals)
    {
        CAPTURE(std::string(type->typeId));
        CHECK(type->category == Category::Dynamics);
        CHECK(BlockRegistry::builtin().find(type->typeId) == type);
        CHECK(type->meterName != nullptr);
        auto block = make(*type);
        // a guitar's level: -20 dBFS peaks
        for (const double hz : { 110.0, 440.0, 2000.0 })
        {
            CAPTURE(hz);
            const Response r = respond(*block, hz, 0.1f);
            CHECK(r.gainDb > -8.0);
            CHECK(r.gainDb < 8.0);
        }
        // and they compress: 30 dB more in is much less than 30 dB more out, and the meter shows it
        const double quiet = respond(*block, 220.0, 0.01f).gainDb;
        const double loud = respond(*block, 220.0, 0.316f).gainDb;
        CHECK(quiet - loud > 4.0);
        CHECK(block->meter() > 3.0f);
    }
}

TEST_CASE("the compressor pedals don't allocate, and Mix or Blend at 0 is the dry signal")
{
    struct Case {
        const BlockDescriptor* type;
        int mix;
    };
    for (const Case c : { Case { &kDiodeBridgeBlockDescriptor, kBridgeMix }, Case { &kConsoleCompBlockDescriptor, kConsoleMix },
                          Case { &kOptoLevelerBlockDescriptor, kLevelerMix }, Case { &kOptoStompBlockDescriptor, kOptoStompBlend },
                          Case { &kFetLimiterBlockDescriptor, kFetMix }, Case { &kSquashCompBlockDescriptor, kSquashBlend } })
    {
        CAPTURE(std::string(c.type->typeId));
        auto block = make(*c.type);
        block->setParam(c.mix, 0.0f);
        std::vector<std::vector<float>> x = { sine(220.0, 0.5f)[0], sine(330.0, 0.4f)[0] };
        const auto dry = x;
        int allocations = 0;
        testing::runBlock(*block, x, 128, &allocations);
        CHECK(allocations == 0);
        double worst = 0.0;
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < kLength; ++i)
                worst = std::max(worst, static_cast<double>(std::fabs(x[ch][i] - dry[ch][i])));
        CHECK(worst < 1e-5);
    }
}

TEST_CASE("diode bridge: the ratio steps, distortion that grows with the reduction, the limiter")
{
    auto bridge = make(kDiodeBridgeBlockDescriptor);
    bridge->setParam(kBridgeThreshold, -30.0f);
    bridge->setParam(kBridgeRecovery, 1.0f); // 400 ms
    bridge->setParam(kBridgeRatio, 0.0f);    // 1.5:1
    const Response gentle = respond(*bridge, 220.0, 0.316f);
    bridge->setParam(kBridgeRatio, 4.0f); // 6:1
    const Response firm = respond(*bridge, 220.0, 0.316f);
    // 20 dB over a -30 dB threshold: about 6.7 dB down at 1.5:1, 16.7 at 6:1 (less with the knee and the peaks)
    CHECK(gentle.gainDb < -3.0);
    CHECK(firm.gainDb < gentle.gainDb - 6.0);
    CHECK(firm.secondDb > -50.0); // the bridge colours as it works
    bridge->setParam(kBridgeThreshold, 0.0f);
    CHECK(respond(*bridge, 220.0, 0.1f).secondDb < firm.secondDb - 10.0);

    // the limiter holds the peaks under Limit whatever the Gain
    bridge->setParam(kBridgeGain, 24.0f);
    bridge->setParam(kBridgeLimit, -12.0f);
    auto x = sine(220.0, 0.2f);
    bridge->reset();
    testing::runBlock(*bridge, x);
    float peak = 0.0f;
    for (size_t i = kLength / 2; i < kLength; ++i)
        peak = std::max(peak, std::fabs(x[0][i]));
    CHECK(20.0f * std::log10(peak) < -9.0f);
    CHECK(20.0f * std::log10(peak) > -16.0f);
}

TEST_CASE("diode bridge: Auto recovery lets go of short peaks faster than of long passages")
{
    auto bridge = make(kDiodeBridgeBlockDescriptor);
    bridge->setParam(kBridgeThreshold, -30.0f);
    bridge->setParam(kBridgeRecovery, 4.0f); // Auto 1
    const float afterShort = reductionAfter(*bridge, 60.0, 300.0);
    const float afterLong = reductionAfter(*bridge, 3000.0, 300.0);
    CHECK(afterLong > afterShort * 1.5f);
    // a fixed time recovers the same either way
    bridge->setParam(kBridgeRecovery, 1.0f); // 400 ms
    const float fixedShort = reductionAfter(*bridge, 60.0, 300.0);
    const float fixedLong = reductionAfter(*bridge, 3000.0, 300.0);
    CHECK(fixedLong < fixedShort * 1.3f);
}

TEST_CASE("console comp: Drive colours the preamp and keeps about its level; Attack lets the pick through")
{
    auto console = make(kConsoleCompBlockDescriptor);
    console->setParam(kConsoleThreshold, 0.0f); // the compressor idle
    console->setParam(kConsoleDrive, 0.0f);
    const Response clean = respond(*console, 220.0, 0.2f);
    console->setParam(kConsoleDrive, 100.0f);
    const Response driven = respond(*console, 220.0, 0.2f);
    CHECK(std::fabs(clean.gainDb) < 1.0);
    CHECK(clean.secondDb < -45.0);
    CHECK(clean.thirdDb < -60.0);
    CHECK(driven.secondDb > clean.secondDb + 10.0);
    CHECK(driven.thirdDb > clean.thirdDb + 20.0);
    CHECK(std::fabs(driven.gainDb) < 9.0);

    // a slower attack lets more of a note's start through before it turns down
    const auto firstMsPeak = [&](float attack) {
        console->setParam(kConsoleDrive, 0.0f);
        console->setParam(kConsoleThreshold, -30.0f);
        console->setParam(kConsoleRatio, 4.0f);
        console->setParam(kConsoleAttack, attack);
        auto x = sine(1000.0, 0.5f, 4800);
        console->reset();
        testing::runBlock(*console, x);
        float peak = 0.0f;
        for (size_t i = 240; i < 960; ++i) // 5-20 ms in
            peak = std::max(peak, std::fabs(x[0][i]));
        return peak;
    };
    CHECK(firstMsPeak(2.0f) > firstMsPeak(0.0f) * 1.5f);
}

TEST_CASE("opto leveler: Limit holds harder, Emphasis listens to the treble, and the cell remembers")
{
    auto opto = make(kOptoLevelerBlockDescriptor);
    opto->setParam(kLevelerPeakReduction, 70.0f);
    const double compress = respond(*opto, 220.0, 0.316f).gainDb;
    opto->setParam(kLevelerMode, 1.0f);
    CHECK(respond(*opto, 220.0, 0.316f).gainDb < compress - 3.0);
    opto->setParam(kLevelerMode, 0.0f);

    const double treble = respond(*opto, 5000.0, 0.1f).gainDb;
    const double bass = respond(*opto, 100.0, 0.1f).gainDb;
    opto->setParam(kLevelerEmphasis, 100.0f);
    CHECK(respond(*opto, 5000.0, 0.1f).gainDb < treble - 3.0);
    CHECK(respond(*opto, 100.0, 0.1f).gainDb > bass - 1.0);
    opto->setParam(kLevelerEmphasis, 0.0f);

    // half lets go quickly; the rest more slowly the longer it has been working
    opto->setParam(kLevelerPeakReduction, 40.0f);
    const float shortStop = reductionAfter(*opto, 100.0, 0.0);
    const float longStop = reductionAfter(*opto, 5000.0, 0.0);
    CHECK(reductionAfter(*opto, 5000.0, 150.0) < longStop * 0.65f);
    const float shortLeft = reductionAfter(*opto, 100.0, 1000.0) / shortStop;
    const float longLeft = reductionAfter(*opto, 5000.0, 1000.0) / longStop;
    CHECK(longLeft > shortLeft * 1.3f);
}

TEST_CASE("opto stomp: the pick gets through the slow cell, and the ratio stays gentle")
{
    auto opto = make(kOptoStompBlockDescriptor);
    opto->setParam(kOptoStompSustain, 100.0f);
    auto x = sine(1000.0, 0.3f, 24000);
    testing::runBlock(*opto, x);
    float start = 0.0f, settled = 0.0f;
    for (size_t i = 0; i < 96; ++i) // the first 2 ms
        start = std::max(start, std::fabs(x[0][i]));
    for (size_t i = 12000; i < 24000; ++i)
        settled = std::max(settled, std::fabs(x[0][i]));
    CHECK(start > settled * 2.0f);

    // 20 dB more in comes out between 3 and 12 dB louder: about 3:1, not a limiter
    const double soft = respond(*opto, 220.0, 0.03f).gainDb;
    const double loud = respond(*opto, 220.0, 0.3f).gainDb;
    CHECK(20.0 + loud - soft > 3.0);
    CHECK(20.0 + loud - soft < 12.0);
}

TEST_CASE("FET limiter: Input pushes it into the threshold with the level compensated; All over-compresses")
{
    auto fet = make(kFetLimiterBlockDescriptor);
    fet->setParam(kFetInput, 0.0f);
    CHECK(std::fabs(respond(*fet, 220.0, 0.1f).gainDb) < 0.5); // under the threshold: unity
    CHECK(fet->meter() < 0.1f);
    fet->setParam(kFetInput, 30.0f);
    const Response pushed = respond(*fet, 220.0, 0.1f);
    CHECK(fet->meter() > 12.0f);
    CHECK(pushed.gainDb < -12.0);
    CHECK(pushed.secondDb > -50.0); // the FET and the output stage add their grit

    // at 20:1 more in is barely more out; with all buttons in it comes out quieter
    fet->setParam(kFetInput, 20.0f);
    fet->setParam(kFetRatio, 3.0f);
    const double a = respond(*fet, 220.0, 0.1f).gainDb;
    const double b = respond(*fet, 220.0, 0.316f).gainDb;
    CHECK(10.0 + b - a < 2.0);
    CHECK(10.0 + b - a > -0.5);
    fet->setParam(kFetRatio, 4.0f);
    const double c = respond(*fet, 220.0, 0.1f).gainDb;
    const double d = respond(*fet, 220.0, 0.316f).gainDb;
    CHECK(10.0 + d - c < 0.0);
}

TEST_CASE("squash comp brings quiet notes up and holds loud ones")
{
    auto squash = make(kSquashCompBlockDescriptor);
    squash->setParam(kSquashSustain, 70.0f);
    const double quiet = respond(*squash, 330.0, 0.003f).gainDb;
    const double loud = respond(*squash, 330.0, 0.3f).gainDb;
    CHECK(quiet > 15.0);
    CHECK(loud < 0.0);
    // 40 dB of difference in comes out as under 15
    CHECK(40.0 + loud - quiet < 15.0);
    squash->setParam(kSquashSustain, 0.0f);
    CHECK(std::fabs(respond(*squash, 330.0, 0.003f).gainDb) < 1.0);
}

// The pedals' levels, harmonics and reduction at their defaults: XYTHOS_COMP_LEVELS=1 ./build/xythos-tests -tc="compressor pedal levels*"
TEST_CASE("compressor pedal levels at the defaults" * doctest::skip(std::getenv("XYTHOS_COMP_LEVELS") == nullptr))
{
    for (const BlockDescriptor* type : kPedals)
    {
        auto block = make(*type);
        std::printf("%-12s", type->typeId);
        for (const float amplitude : { 0.01f, 0.03f, 0.1f, 0.3f })
        {
            const Response r = respond(*block, 220.0, amplitude);
            std::printf("  %4.0f dBFS: %+5.1f dB (h2 %+4.0f h3 %+4.0f, -%4.1f)", 20.0 * std::log10(amplitude), r.gainDb,
                        r.secondDb, r.thirdDb, static_cast<double>(block->meter()));
        }
        std::printf("\n");
    }
}
