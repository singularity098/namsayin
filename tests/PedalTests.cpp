// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// Behaviour tests for the pedal blocks: dynamics, EQ, modulation and wah

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/TempoSync.hpp"
#include "engine/blocks/BitcrusherBlock.hpp"
#include "engine/blocks/ChorusBlock.hpp"
#include "engine/blocks/DoublerBlock.hpp"
#include "engine/blocks/EqBlocks.hpp"
#include "engine/blocks/FlangerBlock.hpp"
#include "engine/blocks/GateBlock.hpp"
#include "engine/blocks/OctaveBlock.hpp"
#include "engine/blocks/PhaserBlock.hpp"
#include "engine/blocks/RingModBlock.hpp"
#include "engine/blocks/RotaryBlock.hpp"
#include "engine/blocks/TremoloBlock.hpp"
#include "engine/blocks/VolumeBlocks.hpp"
#include "engine/blocks/WahBlocks.hpp"

#include <cmath>
#include <vector>

using namespace namsayin;

namespace {

constexpr double kRate = 48000.0;

std::vector<std::vector<float>> sine(double hz, float amplitude, size_t length, int channels = 1)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length));
    for (auto& ch : x)
        for (size_t i = 0; i < length; ++i)
            ch[i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kRate));
    return x;
}

// Steady-state gain in dB of a block for a sine
double sineGainDb(Block& block, double hz, float amplitude = 0.1f, int channels = 1, int channel = 0)
{
    auto x = sine(hz, amplitude, 48000, channels);
    testing::runBlock(block, x);
    const double out = testing::rms(x[channel], 24000, 48000);
    return 20.0 * std::log10(out / (amplitude / std::sqrt(2.0)));
}

std::unique_ptr<Block> make(const BlockDescriptor& type)
{
    return testing::createWithDefaults(type, kRate);
}

// RMS level over windows of `window` samples
std::vector<double> envelope(const std::vector<float>& x, size_t window)
{
    std::vector<double> result;
    for (size_t t = 0; t + window <= x.size(); t += window)
        result.push_back(testing::rms(x, t, t + window));
    return result;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Noise gate

TEST_CASE("gate passes signals over the threshold and cuts those under it")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamThreshold, -40.0f);
    block->setParam(kGateParamReduction, 60.0f);
    CHECK(sineGainDb(*block, 200.0, 0.1f) == doctest::Approx(0.0).epsilon(0.01)); // -20 dB peak: open
    CHECK(block->meter() < 0.1f);
    CHECK(sineGainDb(*block, 200.0, 0.001f) < -55.0); // -60 dB peak: closed
    CHECK(block->meter() == doctest::Approx(60.0f).epsilon(0.01));
}

TEST_CASE("gate holds, then releases after the signal drops")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamThreshold, -40.0f);
    block->setParam(kGateParamHold, 100.0f);
    block->setParam(kGateParamRelease, 10.0f);
    auto x = sine(200.0, 0.1f, 48000);
    for (size_t i = 12000; i < x[0].size(); ++i)
        x[0][i] *= 0.003f; // -50 dB below the open level: under the threshold
    testing::runBlock(*block, x);
    const double open = testing::rms(x[0], 6000, 12000);
    // the detector takes a little while to fall, then 100 ms hold: still open 60 ms after the drop
    CHECK(testing::rms(x[0], 14400, 14880) / 0.003 == doctest::Approx(open / 1.0).epsilon(0.1));
    // well after hold + release: closed
    CHECK(testing::rms(x[0], 24000, 48000) < open * 0.003 * 0.01);
}

TEST_CASE("gate can listen to the rig input instead of its own")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamThreshold, -40.0f);
    block->setParam(kGateParamKey, static_cast<float>(GateKey::Rig));
    auto x = sine(200.0, 0.5f, 24000);
    std::vector<float> rig(24000, 0.0f); // the guitar is silent; the block's input is loud (e.g. amp hiss)
    for (size_t t = 0; t < 24000; t += 120)
    {
        float* io[1] = { x[0].data() + t };
        const float* key[1] = { rig.data() + t };
        block->setRigInput(key, 1);
        block->process(io, 1, 120);
    }
    CHECK(testing::rms(x[0], 12000, 24000) < 1e-4);
}

TEST_CASE("expander attenuates in proportion to how far under the threshold the signal is")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamType, static_cast<float>(GateType::Expander));
    block->setParam(kGateParamThreshold, -30.0f);
    block->setParam(kGateParamHysteresis, 0.0f);
    // a square keeps the peak detector steady: 10 dB under the threshold -> 20 dB of attenuation (ratio 3)
    std::vector<std::vector<float>> x(1, std::vector<float>(48000));
    for (size_t i = 0; i < x[0].size(); ++i)
        x[0][i] = (i / 48) % 2 ? 0.01f : -0.01f; // -40 dB
    testing::runBlock(*block, x);
    CHECK(block->meter() == doctest::Approx(20.0f).epsilon(0.02));
}

// ---------------------------------------------------------------------------------------------------------------------
// EQ

TEST_CASE("graphic EQ boosts and cuts its bands")
{
    auto block = make(kGraphicEqBlockDescriptor);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(0.0).epsilon(0.01));
    block->setParam(3, 12.0f); // 800 Hz
    block->setParam(6, -12.0f); // 6.4 kHz
    CHECK(sineGainDb(*block, 800.0) == doctest::Approx(12.0).epsilon(0.03));
    CHECK(sineGainDb(*block, 6400.0) == doctest::Approx(-12.0).epsilon(0.03));
    CHECK(std::fabs(sineGainDb(*block, 100.0)) < 0.5);
    block->setParam(kGraphicEqParamLevel, -6.0f);
    CHECK(sineGainDb(*block, 100.0) == doctest::Approx(-6.0).epsilon(0.1));
}

TEST_CASE("parametric EQ: mid band, cuts off at the ends of their ranges, and cutting")
{
    auto block = make(kParametricEqBlockDescriptor);
    CHECK(std::fabs(sineGainDb(*block, 30.0)) < 0.05);
    CHECK(std::fabs(sineGainDb(*block, 15000.0)) < 0.05);
    block->setParam(kParEqParamMidFreq, 1000.0f);
    block->setParam(kParEqParamMid, 9.0f);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(9.0).epsilon(0.03));
    block->setParam(kParEqParamLowCut, 300.0f);
    CHECK(sineGainDb(*block, 50.0) < -25.0);
    block->setParam(kParEqParamHighCut, 3000.0f);
    CHECK(sineGainDb(*block, 15000.0) < -25.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tremolo

TEST_CASE("tremolo modulates the level at its rate and depth")
{
    auto block = make(kTremoloBlockDescriptor);
    block->setParam(kTremParamRate, 4.0f);
    block->setParam(kTremParamDepth, 100.0f);
    auto x = sine(2000.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    // 4 Hz: one cycle per 12000 samples; with 480-sample windows, 25 windows per cycle
    const auto env = envelope(x[0], 480);
    const double peak = *std::max_element(env.begin(), env.end());
    const double dip = *std::min_element(env.begin(), env.end());
    CHECK(peak == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.05));
    CHECK(dip < peak * 0.05);
    // the loudest windows repeat every 25 windows
    const size_t first = std::max_element(env.begin(), env.begin() + 25) - env.begin();
    const size_t second = std::max_element(env.begin() + 25, env.begin() + 50) - env.begin();
    CHECK(second - first == 25);
}

TEST_CASE("tremolo follows the host tempo when synced")
{
    auto block = make(kTremoloBlockDescriptor);
    const ParamSpec& rate = kTremoloBlockDescriptor.params[kTremParamRate];
    // pick 1/4 at 120 BPM: 2 Hz
    float n = 0.0f;
    while (rateNoteIndex(n) != 5)
        n += 0.01f;
    block->setParam(kTremParamRate, rate.toValue(n));
    block->setParam(kTremParamSync, 1.0f);
    block->setParam(kTremParamDepth, 100.0f);
    block->setTempo(120.0);
    auto x = sine(2000.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    const auto env = envelope(x[0], 480); // 50 windows per cycle at 2 Hz
    const size_t first = std::max_element(env.begin(), env.begin() + 50) - env.begin();
    const size_t second = std::max_element(env.begin() + 50, env.begin() + 100) - env.begin();
    CHECK(second - first == 50);
}

TEST_CASE("harmonic tremolo at zero depth and pan's power are flat")
{
    auto block = make(kTremoloBlockDescriptor);
    block->setParam(kTremParamType, static_cast<float>(TremoloType::Harmonic));
    block->setParam(kTremParamDepth, 0.0f);
    CHECK(std::fabs(sineGainDb(*block, 300.0)) < 0.01);
    CHECK(std::fabs(sineGainDb(*block, 3000.0)) < 0.01);

    block->setParam(kTremParamType, static_cast<float>(TremoloType::Pan));
    block->setParam(kTremParamDepth, 100.0f);
    auto x = sine(1000.0, 0.5f, 48000, 2);
    testing::runBlock(*block, x);
    const auto left = envelope(x[0], 480), right = envelope(x[1], 480);
    for (size_t w = 0; w < left.size(); ++w)
        CHECK(left[w] * left[w] + right[w] * right[w] == doctest::Approx(2 * 0.125).epsilon(0.05));
}

// ---------------------------------------------------------------------------------------------------------------------
// Chorus, flanger, phaser, doubler

TEST_CASE("chorus at zero mix is dry, and vibrato keeps the level")
{
    auto block = make(kChorusBlockDescriptor);
    block->setParam(kChorusParamMix, 0.0f);
    auto x = sine(440.0, 0.5f, 24000);
    const auto dry = x;
    testing::runBlock(*block, x);
    for (size_t i = 0; i < x[0].size(); ++i)
        REQUIRE(x[0][i] == doctest::Approx(dry[0][i]).epsilon(1e-5));

    block->setParam(kChorusParamType, static_cast<float>(ChorusType::Vibrato));
    block->setParam(kChorusParamTone, 20000.0f);
    CHECK(std::fabs(sineGainDb(*block, 440.0)) < 0.2);
}

TEST_CASE("flanger without sweep is a comb filter at the Manual delay")
{
    auto block = make(kFlangerBlockDescriptor);
    block->setParam(kFlangerParamDepth, 0.0f);
    block->setParam(kFlangerParamManual, 1.0f);
    block->setParam(kFlangerParamFeedback, 0.0f);
    block->setParam(kFlangerParamMix, 50.0f);
    // 1 ms: notches at 500 Hz, 1500 Hz...; peaks at 1 kHz, 2 kHz... (3 dB up: the 50% mix's makeup gain)
    CHECK(sineGainDb(*block, 500.0) < -30.0);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(3.0).epsilon(0.02));
}

TEST_CASE("phaser without sweep notches where its stages reach 180 degrees")
{
    auto block = make(kPhaserBlockDescriptor);
    block->setParam(kPhaserParamDepth, 0.0f);
    block->setParam(kPhaserParamCenter, 1000.0f);
    block->setParam(kPhaserParamFeedback, 0.0f);
    block->setParam(kPhaserParamMix, 50.0f);
    // four first-order allpasses at 1 kHz: -180 degrees total at 1000 * tan(22.5 deg) and 1000 * tan(67.5 deg)
    CHECK(sineGainDb(*block, 1000.0 * std::tan(M_PI / 8.0)) < -30.0);
    CHECK(sineGainDb(*block, 1000.0 * std::tan(3.0 * M_PI / 8.0)) < -30.0);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(3.0).epsilon(0.02)); // with the 50% mix's makeup
}

TEST_CASE("doubler pans a delayed take out to one side")
{
    auto block = make(kDoublerBlockDescriptor);
    block->setParam(kDoublerParamDrift, 0.0f);
    block->setParam(kDoublerParamTone, 20000.0f);
    std::vector<std::vector<float>> x(2, std::vector<float>(4800, 0.0f));
    x[0][0] = x[1][0] = 1.0f;
    testing::runBlock(*block, x);
    // 18 ms = 864 samples: on the right only (width 100%)
    double leftLate = 0.0, rightLate = 0.0;
    for (size_t i = 800; i < 1000; ++i)
    {
        leftLate += std::fabs(x[0][i]);
        rightLate += std::fabs(x[1][i]);
    }
    CHECK(leftLate < 1e-3);
    CHECK(rightLate > 0.5);
    CHECK(x[0][0] == doctest::Approx(1.0f)); // dry stays at full level at 50% mix
}

// ---------------------------------------------------------------------------------------------------------------------
// Wah

TEST_CASE("wah: toe down brings the highs up")
{
    auto block = make(kWahBlockDescriptor);
    block->setParam(kWahParamPosition, 0.0f);
    const double heel = sineGainDb(*block, 2000.0);
    block->setParam(kWahParamPosition, 100.0f);
    const double toe = sineGainDb(*block, 2000.0);
    CHECK(toe - heel > 15.0);
}

TEST_CASE("auto wah opens up with louder playing")
{
    auto block = make(kAutoWahBlockDescriptor);
    block->setParam(kAutoWahParamSensitivity, 20.0f);
    const double quiet = sineGainDb(*block, 2000.0, 0.002f);
    const double loud = sineGainDb(*block, 2000.0, 0.2f);
    CHECK(loud - quiet > 10.0);
    // Down: the other way round
    block->setParam(kAutoWahParamDirection, 1.0f);
    CHECK(sineGainDb(*block, 2000.0, 0.002f) - sineGainDb(*block, 2000.0, 0.2f) > 10.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Octave, volume, swell, ring mod, bitcrusher, rotary

namespace {

// Amplitude of one frequency in x[from, to) (a single-bin DFT with a Hann window)
double toneAt(const std::vector<float>& x, double hz, size_t from, size_t to)
{
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

} // namespace

TEST_CASE("octave divides by two and four, and rectifies up")
{
    struct Case {
        int param;
        double hz;
    };
    for (const Case c : { Case { kOctParamSub1, 110.0 }, Case { kOctParamSub2, 55.0 }, Case { kOctParamUp, 440.0 } })
    {
        CAPTURE(c.param);
        auto block = make(kOctaveBlockDescriptor);
        block->setParam(kOctParamSub1, 0.0f);
        block->setParam(kOctParamDry, 0.0f);
        block->setParam(c.param, 100.0f);
        auto x = sine(220.0, 0.3f, 48000);
        testing::runBlock(*block, x);
        // the new octave is the strongest component
        CHECK(toneAt(x[0], c.hz, 24000, 48000) > 2.0 * toneAt(x[0], 220.0, 24000, 48000));
    }
}

TEST_CASE("volume pedal tapers")
{
    auto block = make(kVolumeBlockDescriptor);
    block->setParam(kVolumeParamVolume, 50.0f);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(-30.0).epsilon(0.01));
    block->setParam(kVolumeParamTaper, 1.0f);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(-6.02).epsilon(0.01));
    block->setParam(kVolumeParamVolume, 0.0f);
    auto x = sine(1000.0, 0.5f, 24000);
    testing::runBlock(*block, x);
    CHECK(testing::rms(x[0], 12000, 24000) == 0.0);
}

TEST_CASE("auto swell fades each new note in")
{
    auto block = make(kSwellBlockDescriptor);
    block->setParam(kSwellParamAttack, 300.0f);
    // a note, a pause, then another note
    auto x = sine(220.0, 0.3f, 72000);
    for (size_t i = 24000; i < 36000; ++i)
        x[0][i] = 0.0f;
    testing::runBlock(*block, x);
    const double full = 0.3 / std::sqrt(2.0);
    CHECK(testing::rms(x[0], 0, 2400) < 0.1 * full);         // the first 50 ms: barely started
    CHECK(testing::rms(x[0], 16800, 24000) > 0.95 * full);   // after the swell: full level
    CHECK(testing::rms(x[0], 36000, 38400) < 0.1 * full);    // the next note swells again
    CHECK(testing::rms(x[0], 52800, 60000) > 0.95 * full);
}

TEST_CASE("ring mod makes sum and difference tones")
{
    auto block = make(kRingModBlockDescriptor);
    block->setParam(kRingParamFrequency, 300.0f);
    auto x = sine(1000.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    CHECK(toneAt(x[0], 700.0, 24000, 48000) == doctest::Approx(0.25).epsilon(0.02));
    CHECK(toneAt(x[0], 1300.0, 24000, 48000) == doctest::Approx(0.25).epsilon(0.02));
    CHECK(toneAt(x[0], 1000.0, 24000, 48000) < 0.005);
}

TEST_CASE("bitcrusher quantizes and holds")
{
    auto block = make(kBitcrusherBlockDescriptor);
    block->setParam(kCrushParamBits, 2.0f); // steps of 0.5
    block->setParam(kCrushParamRate, 6000.0f); // hold each value for 8 samples
    block->setParam(kCrushParamTone, 20000.0f);
    auto x = sine(100.0, 0.9f, 4800);
    testing::runBlock(*block, x);
    size_t lastChange = 0;
    int changes = 0;
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        REQUIRE(std::fabs(x[0][i] * 2.0f - std::round(x[0][i] * 2.0f)) < 1e-6f);
        if (i > 0 && x[0][i] != x[0][i - 1])
        {
            if (changes++ > 0)
                REQUIRE(i - lastChange >= 8);
            lastChange = i;
        }
    }
    CHECK(changes > 10);
}

TEST_CASE("rotary speeds up gradually and its mix at zero is dry")
{
    auto block = make(kRotaryBlockDescriptor);
    block->setParam(kRotaryParamWidth, 0.0f);
    auto x = sine(3000.0, 0.3f, 48000 * 6);
    // a little settle at slow, then switch to fast
    std::vector<std::vector<float>> first(1, std::vector<float>(x[0].begin(), x[0].begin() + 4800));
    testing::runBlock(*block, first);
    block->setParam(kRotaryParamSpeed, 1.0f);
    testing::runBlock(*block, x);

    // count the horn's level swings per second, early and late
    auto swings = [&](size_t from) {
        const auto env = envelope(std::vector<float>(x[0].begin() + from, x[0].begin() + from + 48000), 240);
        double mean = 0.0;
        for (double e : env)
            mean += e;
        mean /= static_cast<double>(env.size());
        int crossings = 0;
        for (size_t w = 1; w < env.size(); ++w)
            if (env[w - 1] < mean && env[w] >= mean)
                ++crossings;
        return crossings;
    };
    const int early = swings(0);
    const int late = swings(48000 * 5);
    CHECK(early < late);
    CHECK(late == doctest::Approx(6.7).epsilon(0.16));

    block->setParam(kRotaryParamMix, 0.0f);
    auto dry = sine(500.0, 0.3f, 24000);
    const auto reference = dry;
    testing::runBlock(*block, dry);
    CHECK(dry[0][23999] == doctest::Approx(reference[0][23999]).epsilon(1e-4));
}
