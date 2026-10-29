// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/blocks/DriveBlocks.hpp"
#include "engine/dsp/Oversampler.hpp"

#include <cmath>
#include <complex>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;

std::vector<std::vector<float>> sine(double hz, float amplitude, size_t length)
{
    std::vector<std::vector<float>> x(1, std::vector<float>(length));
    for (size_t i = 0; i < length; ++i)
        x[0][i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kRate));
    return x;
}

// Amplitude of one frequency in x[from, to) (a single-bin DFT with a Hann window)
double amplitudeAt(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    std::complex<double> sum = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i - from) / n);
        sum += w * x[i] * std::polar(1.0, -2.0 * M_PI * hz * static_cast<double>(i) / kRate);
    }
    return 4.0 * std::abs(sum) / n;
}

double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

std::unique_ptr<Block> make(const BlockDescriptor& type)
{
    return testing::createWithDefaults(type, kRate);
}

} // namespace

TEST_CASE("half-band filters: flat passband, deep stopband")
{
    // through the 4x oversampler with an identity function: flat up to near Nyquist
    for (const double hz : { 100.0, 5000.0, 18000.0, 22000.0 })
    {
        Oversampler4x os;
        os.prepare();
        auto x = sine(hz, 0.5f, 24000);
        for (float& v : x[0])
            v = os.process(v, [](float s) { return s; });
        CAPTURE(hz);
        CHECK(testing::rms(x[0], 12000, 24000) == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.005));
    }
    // a cubic distortion of 15 kHz makes 45 kHz, which would alias to 3 kHz without oversampling
    Oversampler4x os;
    os.prepare();
    auto x = sine(15000.0, 0.9f, 48000);
    for (float& v : x[0])
        v = os.process(v, [](float s) { return s * s * s; });
    CHECK(db(amplitudeAt(x[0], 3000.0, 24000, 48000)) < -100.0);
}

TEST_CASE("drives add harmonics, more with more drive")
{
    for (const auto* type : { &kOverdriveBlockDescriptor, &kDistortionBlockDescriptor, &kFuzzBlockDescriptor })
    {
        CAPTURE(type->typeId);
        auto block = make(*type);
        block->setParam(0, 10.0f);
        auto low = sine(220.0, 0.3f, 48000);
        testing::runBlock(*block, low);
        block->setParam(0, 100.0f);
        auto high = sine(220.0, 0.3f, 48000);
        testing::runBlock(*block, high);
        // third harmonic relative to the fundamental
        const double lowRatio = amplitudeAt(low[0], 660.0, 24000, 48000) / amplitudeAt(low[0], 220.0, 24000, 48000);
        const double highRatio =
            amplitudeAt(high[0], 660.0, 24000, 48000) / amplitudeAt(high[0], 220.0, 24000, 48000);
        CHECK(highRatio > 0.1);
        CHECK(highRatio > lowRatio);
    }
}

TEST_CASE("drives keep aliasing low")
{
    // 4.7 kHz through maximum distortion: harmonics at 14.1 kHz, 23.5 kHz...; aliases land between them. Measure a
    // few alias frequencies against the fundamental.
    auto block = make(kDistortionBlockDescriptor);
    block->setParam(kDistParamDrive, 100.0f);
    block->setParam(kDistParamTone, 100.0f);
    auto x = sine(4700.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    const double fundamental = amplitudeAt(x[0], 4700.0, 24000, 48000);
    // 5th harmonic 23.5 kHz is below Nyquist; the 7th (32.9 kHz) would alias to 15.1 kHz, the 9th (42.3 kHz) to
    // 5.7 kHz, the 11th (51.7 kHz) to 3.7 kHz
    for (const double alias : { 15100.0, 5700.0, 3700.0 })
    {
        CAPTURE(alias);
        CHECK(db(amplitudeAt(x[0], alias, 24000, 48000) / fundamental) < -60.0);
    }
}

TEST_CASE("drives don't leave DC, whatever their type")
{
    struct Case {
        const BlockDescriptor* type;
        int typeParam;
        int types;
    };
    for (const Case& c : { Case { &kOverdriveBlockDescriptor, kOdParamType, 3 },
                           Case { &kDistortionBlockDescriptor, kDistParamType, 3 },
                           Case { &kFuzzBlockDescriptor, kFuzzParamType, 3 },
                           Case { &kBoostBlockDescriptor, kBoostParamType, 3 } })
    {
        for (int t = 0; t < c.types; ++t)
        {
            CAPTURE(c.type->typeId);
            CAPTURE(t);
            auto block = make(*c.type);
            block->setParam(c.typeParam, static_cast<float>(t));
            block->setParam(0, c.type->params[0].max);
            auto x = sine(110.0, 0.5f, 96000);
            testing::runBlock(*block, x);
            double mean = 0.0;
            for (size_t i = 48000; i < 96000; ++i)
                mean += x[0][i];
            mean /= 48000.0;
            CHECK(std::fabs(mean) < 0.01);
        }
    }
}

TEST_CASE("octave fuzz doubles the frequency")
{
    auto block = make(kFuzzBlockDescriptor);
    block->setParam(kFuzzParamType, static_cast<float>(FuzzType::Octave));
    block->setParam(kFuzzParamTone, 100.0f);
    auto x = sine(220.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    CHECK(amplitudeAt(x[0], 440.0, 24000, 48000) > 2.0 * amplitudeAt(x[0], 220.0, 24000, 48000));
}

TEST_CASE("starved fuzz gates small signals")
{
    auto block = make(kFuzzBlockDescriptor);
    block->setParam(kFuzzParamFuzz, 0.0f);
    block->setParam(kFuzzParamBias, 0.0f);
    auto quiet = sine(220.0, 0.005f, 48000);
    testing::runBlock(*block, quiet);
    CHECK(testing::rms(quiet[0], 24000, 48000) < 1e-4);
}

TEST_CASE("clean boost is clean and adds its gain")
{
    auto block = make(kBoostBlockDescriptor);
    block->setParam(kBoostParamGain, 12.0f);
    auto x = sine(1000.0, 0.1f, 48000);
    testing::runBlock(*block, x);
    CHECK(db(amplitudeAt(x[0], 1000.0, 24000, 48000) / 0.1) == doctest::Approx(12.0).epsilon(0.02));
    CHECK(db(amplitudeAt(x[0], 3000.0, 24000, 48000) / 0.1) < -80.0);
}
