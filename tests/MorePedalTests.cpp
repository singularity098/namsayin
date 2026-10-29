// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// Behaviour tests for the second set of pedals (Requirements, "Pedals": the ones agreed after the first set)

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/TempoSync.hpp"
#include "engine/blocks/AmbientBlocks.hpp"
#include "engine/blocks/BarberPoleBlock.hpp"
#include "engine/blocks/DelayFxBlocks.hpp"
#include "engine/blocks/DriveBlocks.hpp"
#include "engine/blocks/FilterBlock.hpp"
#include "engine/blocks/PitchBlocks.hpp"
#include "engine/blocks/RhythmBlocks.hpp"
#include "engine/blocks/TalkBoxBlock.hpp"
#include "engine/blocks/WarbleBlock.hpp"
#include "engine/dsp/Biquad.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace xythos;

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

// A band-limited sawtooth (harmonics up to 8 kHz): stationary and rich, for filters
std::vector<std::vector<float>> saw(double hz, float amplitude, size_t length)
{
    std::vector<std::vector<float>> x(1, std::vector<float>(length, 0.0f));
    for (int h = 1; h * hz < 8000.0; ++h)
        for (size_t i = 0; i < length; ++i)
            x[0][i] += amplitude / static_cast<float>(h)
                       * static_cast<float>(std::sin(2.0 * M_PI * hz * h * static_cast<double>(i) / kRate));
    return x;
}

std::unique_ptr<Block> make(const BlockDescriptor& type)
{
    return testing::createWithDefaults(type, kRate);
}

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

// Level in a band around `hz` (+-3%): delay-line pitch shifting spreads a tone into sidebands
double bandAt(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    double power = 0.0;
    for (double f = hz * 0.97; f <= hz * 1.03; f += 2.0)
    {
        const double a = toneAt(x, f, from, to);
        power += a * a;
    }
    return std::sqrt(power);
}

// The frequency of a clean tone in x[from, to), from its interpolated rising zero crossings
double zeroCrossingHz(const std::vector<float>& x, size_t from, size_t to)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = from + 1; i < to; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double at = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
                first = at;
            last = at;
            ++count;
        }
    }
    return count > 1 ? (count - 1) * kRate / (last - first) : 0.0;
}

double rmsDb(const std::vector<float>& x, size_t from, size_t to)
{
    return 20.0 * std::log10(std::max(testing::rms(x, from, to), 1e-12));
}

size_t ms(double milliseconds)
{
    return static_cast<size_t>(milliseconds / 1000.0 * kRate);
}

// Runs a block over x in buffers, calling `before(t)` ahead of each buffer (parameter changes, song position)
template <typename F>
void runWith(Block& block, std::vector<std::vector<float>>& x, F&& before, uint32_t buffer = 128)
{
    const size_t length = x[0].size();
    for (size_t t = 0; t < length; t += buffer)
    {
        before(t);
        const auto n = static_cast<uint32_t>(std::min<size_t>(buffer, length - t));
        float* io[2] = { x[0].data() + t, x[x.size() > 1 ? 1 : 0].data() + t };
        block.process(io, static_cast<uint32_t>(x.size()), n);
    }
}

// The normalized knob position that picks note value `name` on a time knob
float timeKnobFor(const char* name)
{
    for (float n = 0.0f; n <= 1.0f; n += 0.001f)
        if (std::string(noteName(timeNoteIndex(n))) == name)
            return n;
    return 0.0f;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Drives

TEST_CASE("envelope drive gets dirtier as you dig in, and cleaner with a negative range")
{
    // the third harmonic against the fundamental, for a soft and a hard note
    auto distortion = [](float drive, float range, float amplitude) {
        auto block = make(kEnvDriveBlockDescriptor);
        block->setParam(kEnvDriveParamDrive, drive);
        block->setParam(kEnvDriveParamRange, range);
        auto x = sine(220.0, amplitude, 24000);
        testing::runBlock(*block, x);
        return toneAt(x[0], 660.0, 12000, 24000) / toneAt(x[0], 220.0, 12000, 24000);
    };
    CHECK(distortion(30.0f, 100.0f, 0.3f) > 3.0 * distortion(30.0f, 100.0f, 0.005f));
    // negative: digging in takes the drive away
    CHECK(distortion(80.0f, -100.0f, 0.3f) < 0.2 * distortion(80.0f, 0.0f, 0.3f));

    // the level follows the playing: soft stays soft and loud stays loud, within a few dB
    for (const float amplitude : { 0.005f, 0.3f })
    {
        CAPTURE(amplitude);
        auto block = make(kEnvDriveBlockDescriptor);
        block->setParam(kEnvDriveParamRange, 100.0f);
        auto x = sine(220.0, amplitude, 24000);
        testing::runBlock(*block, x);
        CHECK(std::fabs(rmsDb(x[0], 12000, 24000) - 20.0 * std::log10(amplitude / std::sqrt(2.0))) < 4.0);
    }
}

TEST_CASE("gated fuzz: the gate cuts quiet playing, low Stab squeals and dies away")
{
    {
        auto block = make(kGatedFuzzBlockDescriptor);
        block->setParam(kGatedFuzzParamGate, 60.0f); // -44 dB
        auto quiet = sine(220.0, 0.001f, 24000);
        testing::runBlock(*block, quiet);
        CHECK(testing::rms(quiet[0], 12000, 24000) < 1e-4);
        auto loud = sine(220.0, 0.1f, 24000);
        testing::runBlock(*block, loud);
        CHECK(testing::rms(loud[0], 12000, 24000) > 0.02);
    }

    // a quiet low note: energy above 1.5 kHz is a few harmonics when stable, the squeal when not
    auto highs = [](float stab, double& tail) {
        auto block = make(kGatedFuzzBlockDescriptor);
        block->setParam(kGatedFuzzParamStab, stab);
        block->setParam(kGatedFuzzParamTone, 100.0f);
        auto x = sine(55.0, 0.02f, 48000 * 3);
        std::fill(x[0].begin() + 48000, x[0].end(), 0.0f);
        testing::runBlock(*block, x);
        Biquad hp;
        hp.setHighPass(kRate, 1500.0);
        std::vector<float> high(x[0].begin(), x[0].begin() + 48000);
        for (float& v : high)
            v = hp.process(v);
        tail = testing::rms(x[0], 48000 * 2, 48000 * 3);
        return testing::rms(high, 24000, 48000);
    };
    double stableTail = 0.0, wildTail = 0.0;
    const double stable = highs(100.0f, stableTail);
    const double wild = highs(0.0f, wildTail);
    CHECK(wild > 3.0 * stable);
    CHECK(wildTail < 1e-4);
}

// ---------------------------------------------------------------------------------------------------------------------
// Dynamics, rhythm

TEST_CASE("kill switch mutes while held, latches, and inverts")
{
    auto block = make(kKillSwitchBlockDescriptor);
    auto x = sine(440.0, 0.5f, ms(400));
    runWith(*block, x, [&](size_t t) { block->setParam(kKillParamKill, t >= ms(100) && t < ms(200) ? 1.0f : 0.0f); });
    CHECK(testing::rms(x[0], ms(20), ms(100)) > 0.3);
    CHECK(testing::rms(x[0], ms(120), ms(200)) < 1e-4);
    CHECK(testing::rms(x[0], ms(220), ms(400)) > 0.3);

    // latched: a press mutes until the next press
    block->setParam(kKillParamLatch, 1.0f);
    auto y = sine(440.0, 0.5f, ms(600));
    runWith(*block, y, [&](size_t t) {
        const bool pressed = (t >= ms(100) && t < ms(150)) || (t >= ms(400) && t < ms(450));
        block->setParam(kKillParamKill, pressed ? 1.0f : 0.0f);
    });
    CHECK(testing::rms(y[0], ms(170), ms(400)) < 1e-4);
    CHECK(testing::rms(y[0], ms(450), ms(600)) > 0.3);

    // inverted: silent until Kill is held
    auto inverted = make(kKillSwitchBlockDescriptor);
    inverted->setParam(kKillParamInvert, 1.0f);
    auto z = sine(440.0, 0.5f, ms(300));
    runWith(*inverted, z, [&](size_t t) { inverted->setParam(kKillParamKill, t >= ms(150) ? 1.0f : 0.0f); });
    CHECK(testing::rms(z[0], ms(20), ms(150)) < 1e-4);
    CHECK(testing::rms(z[0], ms(170), ms(300)) > 0.3);
}

TEST_CASE("kill switch stutter chops at its time and duty")
{
    auto block = make(kKillSwitchBlockDescriptor);
    block->setParam(kKillParamTime, 100.0f);
    block->setParam(kKillParamDuty, 50.0f);
    block->setParam(kKillParamStutter, 1.0f);
    auto x = sine(2000.0, 0.5f, ms(400));
    testing::runBlock(*block, x);
    // on for the first 50 ms of each 100 ms cycle, from the press
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        CAPTURE(cycle);
        const double start = 100.0 * cycle;
        CHECK(testing::rms(x[0], ms(start + 5), ms(start + 45)) > 0.3);
        CHECK(testing::rms(x[0], ms(start + 55), ms(start + 95)) < 1e-3);
    }
}

TEST_CASE("kill switch rate mod: steps double the stutter's speed, and sweep glides through the speeds")
{
    // checks a stretch is on for its first half of each cycle and off for the second
    auto chops = [](const std::vector<float>& x, double from, double cycle, int count) {
        for (int c = 0; c < count; ++c)
        {
            CAPTURE(from);
            CAPTURE(c);
            const double start = from + cycle * c;
            CHECK(testing::rms(x, ms(start + 0.1 * cycle), ms(start + 0.4 * cycle)) > 0.3);
            CHECK(testing::rms(x, ms(start + 0.6 * cycle), ms(start + 0.9 * cycle)) < 1e-3);
        }
    };
    auto stutter = [](int direction, int rateMod) {
        auto block = make(kKillSwitchBlockDescriptor);
        block->setParam(kKillParamTime, 200.0f);
        block->setParam(kKillParamFade, 1.0f);
        block->setParam(kKillParamRateMod, static_cast<float>(rateMod));
        block->setParam(kKillParamSpeeds, 1.0f);  // 3: 200, 100, 50 ms
        block->setParam(kKillParamStepLen, 1.0f); // 2 cycles of 200 ms at each
        block->setParam(kKillParamDirection, static_cast<float>(direction));
        block->setParam(kKillParamStutter, 1.0f);
        auto x = sine(2000.0, 0.5f, ms(2800));
        testing::runBlock(*block, x);
        return x;
    };

    // Faster: 200 ms twice, 100 ms four times, 50 ms eight times, then over again
    const auto faster = stutter(0, 1);
    chops(faster[0], 0.0, 200.0, 2);
    chops(faster[0], 400.0, 100.0, 4);
    chops(faster[0], 800.0, 50.0, 8);
    chops(faster[0], 1200.0, 200.0, 2);
    // Slower: the other way round
    const auto slower = stutter(1, 1);
    chops(slower[0], 0.0, 50.0, 8);
    chops(slower[0], 400.0, 100.0, 4);
    chops(slower[0], 800.0, 200.0, 2);
    // Both: 200, 100, 50, 100, then 200 again
    const auto both = stutter(2, 1);
    chops(both[0], 800.0, 50.0, 8);
    chops(both[0], 1200.0, 100.0, 4);
    chops(both[0], 1600.0, 200.0, 2);

    // Sweep: the gaps get shorter over the sweep, then start long again
    const auto sweep = stutter(0, 2);
    std::vector<double> gaps; // ms each silence lasts, in order
    size_t off = 0;
    for (size_t t = 1; t < sweep[0].size(); ++t)
    {
        const bool silent = std::fabs(sweep[0][t]) < 1e-4f && std::fabs(sweep[0][t - 1]) < 1e-4f;
        if (silent && off == 0)
            off = t;
        if (!silent && off > 0 && std::fabs(sweep[0][t]) > 0.1f)
        {
            gaps.push_back(1000.0 * static_cast<double>(t - off) / kRate);
            off = 0;
        }
    }
    REQUIRE(gaps.size() > 6);
    CHECK(gaps[0] > 60.0);
    CHECK(gaps[0] > gaps[1]);
    CHECK(gaps[1] > gaps[3]);
    CHECK(*std::min_element(gaps.begin(), gaps.end()) < 30.0);
}

TEST_CASE("slicer plays its steps at their levels, and Steps sets the pattern length")
{
    auto block = make(kSlicerBlockDescriptor);
    block->setParam(kSlicerParamTime, 50.0f);
    block->setParam(kSlicerParamSmooth, 0.0f);
    const float levels[kSlicerSteps] = { 100.0f, 0.0f, 50.0f, 0.0f, 100.0f, 25.0f, 0.0f, 100.0f };
    for (int s = 0; s < kSlicerSteps; ++s)
        block->setParam(kSlicerParamStep1 + s, levels[s]);
    auto x = sine(3000.0, 0.5f, ms(800));
    testing::runBlock(*block, x);
    const double full = 0.5 / std::sqrt(2.0);
    for (int step = 0; step < 16; ++step)
    {
        CAPTURE(step);
        const double level = testing::rms(x[0], ms(50.0 * step + 5), ms(50.0 * step + 45));
        CHECK(level == doctest::Approx(full * levels[step % 8] / 100.0).epsilon(0.02).scale(full));
    }

    block->setParam(kSlicerParamSteps, 0.0f); // two steps: 100, 0
    block->reset();
    auto y = sine(3000.0, 0.5f, ms(400));
    testing::runBlock(*block, y);
    for (int step = 0; step < 8; ++step)
    {
        CAPTURE(step);
        const double level = testing::rms(y[0], ms(50.0 * step + 5), ms(50.0 * step + 45));
        CHECK((step % 2 == 0 ? level > 0.3 : level < 1e-3));
    }
}

TEST_CASE("synced slicer starts its pattern on the bar")
{
    auto block = make(kSlicerBlockDescriptor);
    const ParamSpec& time = kSlicerBlockDescriptor.params[kSlicerParamTime];
    block->setParam(kSlicerParamTime, time.toValue(timeKnobFor("1/4")));
    block->setParam(kSlicerParamSync, 1.0f);
    block->setParam(kSlicerParamSmooth, 0.0f);
    block->setParam(kSlicerParamSteps, 2.0f); // four steps
    for (int s = 0; s < kSlicerSteps; ++s)
        block->setParam(kSlicerParamStep1 + s, s == 0 ? 100.0f : 0.0f);
    block->setTempo(120.0);
    // the song is at beat 2.5 when we start: step 1 sounds from beat 4 (0.75 s) for one beat
    auto x = sine(3000.0, 0.5f, 48000 * 2);
    const double beatsPerSample = 120.0 / 60.0 / kRate;
    runWith(*block, x, [&](size_t t) { block->setSongPosition(2.5 + static_cast<double>(t) * beatsPerSample); });
    CHECK(testing::rms(x[0], ms(10), ms(740)) < 1e-3);
    CHECK(testing::rms(x[0], ms(760), ms(1240)) > 0.3);
    CHECK(testing::rms(x[0], ms(1260), ms(2000)) < 1e-3);
}

// ---------------------------------------------------------------------------------------------------------------------
// Modulation

TEST_CASE("barber pole is dry at zero mix, and its notches keep passing a tone")
{
    auto dry = make(kBarberPoleBlockDescriptor);
    dry->setParam(kBarberParamMix, 0.0f);
    auto x = sine(500.0, 0.3f, 24000);
    const auto original = x;
    testing::runBlock(*dry, x);
    for (size_t i = 0; i < x[0].size(); i += 97)
        CHECK(x[0][i] == doctest::Approx(original[0][i]).epsilon(1e-5));

    for (const float direction : { 0.0f, 1.0f })
    {
        CAPTURE(direction);
        auto block = make(kBarberPoleBlockDescriptor);
        block->setParam(kBarberParamRate, 0.5f);
        block->setParam(kBarberParamFeedback, 0.0f);
        block->setParam(kBarberParamDirection, direction);
        auto y = sine(500.0, 0.3f, 48000 * 4);
        testing::runBlock(*block, y);
        // a notch sweeps past 500 Hz again and again, never stopping at the top or bottom
        double loudest = 0.0, quietest = 1.0;
        for (size_t t = 48000; t + 960 <= y[0].size(); t += 960)
        {
            const double level = testing::rms(y[0], t, t + 960);
            loudest = std::max(loudest, level);
            quietest = std::min(quietest, level);
        }
        CHECK(20.0 * std::log10(loudest / quietest) > 6.0);
        CHECK(loudest < 0.5);
    }
}

TEST_CASE("warble wobbles the pitch, and is dry at zero mix")
{
    auto block = make(kWarbleBlockDescriptor);
    block->setParam(kWarbleParamDepth, 100.0f);
    block->setParam(kWarbleParamRate, 2.0f);
    block->setParam(kWarbleParamRandom, 0.0f);
    block->setParam(kWarbleParamFlutter, 0.0f);
    block->setParam(kWarbleParamDrive, 0.0f);
    block->setParam(kWarbleParamTone, 20000.0f);
    auto x = sine(1000.0, 0.3f, 48000 * 2);
    testing::runBlock(*block, x);
    double low = 1e9, high = 0.0;
    for (size_t t = 24000; t + ms(25) <= x[0].size(); t += ms(25))
    {
        const double hz = zeroCrossingHz(x[0], t, t + ms(25));
        low = std::min(low, hz);
        high = std::max(high, hz);
    }
    // about +-25 cents at 0.5 Hz scales with the rate: well over 20 cents each way at 2 Hz
    CHECK(1200.0 * std::log2(high / 1000.0) > 20.0);
    CHECK(1200.0 * std::log2(1000.0 / low) > 20.0);
    // the defaults keep the level
    auto standard = make(kWarbleBlockDescriptor);
    auto y = sine(440.0, 0.3f, 48000);
    testing::runBlock(*standard, y);
    CHECK(std::fabs(rmsDb(y[0], 24000, 48000) - rmsDb(sine(440.0, 0.3f, 48000)[0], 24000, 48000)) < 1.5);

    standard->setParam(kWarbleParamMix, 0.0f);
    auto z = sine(440.0, 0.3f, 24000);
    const auto original = z;
    testing::runBlock(*standard, z); // the mix glides to 0 over a few tens of ms
    for (size_t i = 12000; i < z[0].size(); i += 97)
        CHECK(z[0][i] == doctest::Approx(original[0][i]).epsilon(1e-4));
}

// ---------------------------------------------------------------------------------------------------------------------
// Reverbs

TEST_CASE("bloom reverb swells in after the note instead of on the pick")
{
    // reverb only, a held note: how much comes in over the first 150 ms, against 1.5-2 s in
    auto run = [](float bloomMs) {
        auto block = make(kBloomReverbBlockDescriptor);
        block->setParam(kBloomParamMix, 100.0f);
        block->setParam(kBloomParamBloom, bloomMs);
        auto x = sine(330.0, 0.3f, 48000 * 2);
        testing::runBlock(*block, x);
        return testing::rms(x[0], 0, ms(150)) / testing::rms(x[0], ms(1500), ms(2000));
    };
    CHECK(run(3000.0f) < 0.25 * run(50.0f));
}

TEST_CASE("gated reverb is cut off after its hold")
{
    auto block = make(kGatedReverbBlockDescriptor);
    block->setParam(kGatedRevParamMix, 100.0f);
    block->setParam(kGatedRevParamDecay, 4.0f);
    block->setParam(kGatedRevParamHold, 100.0f);
    block->setParam(kGatedRevParamRelease, 20.0f);
    auto x = sine(330.0, 0.3f, ms(800));
    std::fill(x[0].begin() + static_cast<long>(ms(200)), x[0].end(), 0.0f);
    testing::runBlock(*block, x);
    const double open = testing::rms(x[0], ms(100), ms(200));
    CHECK(open > 0.02);
    // the input stops at 200 ms: the detector falls, the hold runs 100 ms, the gate closes over 20 ms; a 4 s reverb
    // would still be ringing loudly
    CHECK(testing::rms(x[0], ms(450), ms(800)) < 1e-4);
}

// ---------------------------------------------------------------------------------------------------------------------
// Delays

TEST_CASE("reverse delay plays slices backwards")
{
    // a rising chirp: the reversed slices fall in pitch while they play
    auto block = make(kReverseDelayBlockDescriptor);
    block->setParam(kRevDelayParamMix, 100.0f);
    block->setParam(kRevDelayParamFeedback, 0.0f);
    block->setParam(kRevDelayParamTone, 20000.0f);
    block->setParam(kRevDelayParamTime, 500.0f);
    std::vector<std::vector<float>> x(1, std::vector<float>(48000 * 4));
    double phase = 0.0;
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        const double hz = 300.0 * std::exp2(static_cast<double>(i) / kRate / 1.5); // an octave per 1.5 s
        phase += 2.0 * M_PI * hz / kRate;
        x[0][i] = 0.3f * static_cast<float>(std::sin(phase));
    }
    testing::runBlock(*block, x);
    int falling = 0, rising = 0;
    for (size_t t = 48000; t + 2 * ms(30) <= x[0].size(); t += ms(30))
    {
        const double a = zeroCrossingHz(x[0], t, t + ms(30));
        const double b = zeroCrossingHz(x[0], t + ms(30), t + 2 * ms(30));
        (b < a ? falling : rising) += 1;
    }
    CHECK(falling > 2 * rising);
}

TEST_CASE("pitch delay shifts each repeat once more")
{
    auto block = make(kPitchDelayBlockDescriptor); // +12 per repeat
    block->setParam(kPitchDelayParamMix, 100.0f);
    block->setParam(kPitchDelayParamTime, 300.0f);
    block->setParam(kPitchDelayParamTone, 20000.0f);
    auto x = sine(220.0, 0.3f, ms(800));
    std::fill(x[0].begin() + static_cast<long>(ms(100)), x[0].end(), 0.0f);
    testing::runBlock(*block, x);
    // the first repeat (about 320..400 ms) an octave up, the second (620..700 ms) two
    CHECK(bandAt(x[0], 440.0, ms(330), ms(400)) > 4.0 * bandAt(x[0], 220.0, ms(330), ms(400)));
    CHECK(bandAt(x[0], 880.0, ms(630), ms(700)) > 4.0 * bandAt(x[0], 440.0, ms(630), ms(700)));
}

TEST_CASE("beat repeat repeats the last slice while held, then lets your playing back")
{
    auto block = make(kBeatRepeatBlockDescriptor);
    block->setParam(kRepeatParamSlice, 100.0f);
    // 440 Hz for 500 ms, then 880 Hz; Repeat is held from 500 to 1000 ms
    std::vector<std::vector<float>> x(1, std::vector<float>(48000 * 3 / 2));
    for (size_t i = 0; i < x[0].size(); ++i)
        x[0][i] = 0.3f * static_cast<float>(std::sin(2.0 * M_PI * (i < ms(500) ? 440.0 : 880.0) * i / kRate));
    runWith(*block, x, [&](size_t t) { block->setParam(kRepeatParamRepeat, t >= ms(500) && t < ms(1000) ? 1.0f : 0.0f); });
    CHECK(toneAt(x[0], 440.0, ms(600), ms(1000)) > 0.2);
    CHECK(toneAt(x[0], 880.0, ms(600), ms(1000)) < 0.02);
    CHECK(toneAt(x[0], 880.0, ms(1050), ms(1500)) > 0.25);

    // Decay fades the repeats, and Latch keeps them going after the press
    auto latched = make(kBeatRepeatBlockDescriptor);
    latched->setParam(kRepeatParamSlice, 100.0f);
    latched->setParam(kRepeatParamDecay, 30.0f);
    latched->setParam(kRepeatParamLatch, 1.0f);
    auto y = sine(440.0, 0.3f, ms(1000));
    runWith(*latched, y, [&](size_t t) { latched->setParam(kRepeatParamRepeat, t >= ms(300) && t < ms(320) ? 1.0f : 0.0f); });
    const double first = testing::rms(y[0], ms(305), ms(395));
    const double third = testing::rms(y[0], ms(505), ms(595));
    CHECK(third == doctest::Approx(first * 0.49).epsilon(0.1));
}

// ---------------------------------------------------------------------------------------------------------------------
// Pitch

TEST_CASE("detune voices sit a few cents off, spread left and right")
{
    auto block = make(kDetuneBlockDescriptor);
    block->setParam(kDetuneParamDetune, 50.0f);
    block->setParam(kDetuneParamMix, 100.0f);
    block->setParam(kDetuneParamWidth, 100.0f);
    auto x = sine(440.0, 0.3f, 96000, 2);
    testing::runBlock(*block, x);
    const double up = 440.0 * std::exp2(0.5 / 12.0), down = 440.0 * std::exp2(-0.5 / 12.0);
    CHECK(toneAt(x[0], up, 48000, 96000) > 4.0 * toneAt(x[0], down, 48000, 96000));
    CHECK(toneAt(x[1], down, 48000, 96000) > 4.0 * toneAt(x[1], up, 48000, 96000));
}

TEST_CASE("capo shifts by whole semitones with no dry signal")
{
    auto block = make(kCapoBlockDescriptor);
    block->setParam(kCapoParamShift, -2.0f);
    auto x = sine(440.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    const double target = 440.0 * std::exp2(-2.0 / 12.0);
    CHECK(bandAt(x[0], target, 24000, 48000) > 10.0 * toneAt(x[0], 440.0, 24000, 48000));
    CHECK(testing::rms(x[0], 24000, 48000) == doctest::Approx(0.3 / std::sqrt(2.0)).epsilon(0.1));
}

TEST_CASE("organ stacks its stops, and swells each note in")
{
    auto block = make(kOrganBlockDescriptor);
    block->setParam(kOrganParamTwelfth, 60.0f);
    block->setParam(kOrganParamTone, 20000.0f);
    auto x = sine(220.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    for (const double hz : { 110.0, 220.0, 440.0, 659.3, 880.0 })
    {
        CAPTURE(hz);
        CHECK(bandAt(x[0], hz, 24000, 48000) > 0.03);
    }

    auto slow = make(kOrganBlockDescriptor);
    slow->setParam(kOrganParamAttack, 300.0f);
    auto y = sine(220.0, 0.3f, 48000);
    testing::runBlock(*slow, y);
    CHECK(testing::rms(y[0], 0, ms(30)) < 0.1 * testing::rms(y[0], ms(400), ms(500)));
}

TEST_CASE("arpeggiator notes and patterns")
{
    CHECK(arpNoteCount(1, 1) == 4); // major: root, third, fifth, octave
    CHECK(arpNote(1, 1, 1) == 4);
    CHECK(arpNote(1, 1, 3) == 12);
    CHECK(arpNoteCount(1, 2) == 7);
    CHECK(arpNote(1, 2, 5) == 19);
    CHECK(arpNote(0, 2, 2) == 24); // octaves over two octaves: 0, 12, 24
    CHECK(arpPatternIndex(0, 5, 4) == 1);
    CHECK(arpPatternIndex(1, 0, 4) == 3);
    const int upDown[] = { 0, 1, 2, 3, 2, 1, 0, 1 };
    for (int s = 0; s < 8; ++s)
        CHECK(arpPatternIndex(2, s, 4) == upDown[s]);
}

TEST_CASE("arpeggiator steps a held note through the chord")
{
    auto block = make(kArpeggiatorBlockDescriptor); // major, up, one octave
    block->setParam(kArpParamRate, 4.0f);
    block->setParam(kArpParamGate, 100.0f);
    auto x = sine(220.0, 0.3f, 48000 * 2);
    testing::runBlock(*block, x);
    const int notes[] = { 0, 4, 7, 12, 0, 4, 7, 12 };
    for (int step = 1; step < 8; ++step)
    {
        CAPTURE(step);
        const double hz = 220.0 * std::exp2(notes[step] / 12.0);
        const size_t from = ms(250.0 * step + 80), to = ms(250.0 * step + 240);
        const double other = 220.0 * std::exp2(notes[(step + 1) % 4] / 12.0);
        CHECK(bandAt(x[0], hz, from, to) > 3.0 * bandAt(x[0], other, from, to));
    }
}

TEST_CASE("sustainer holds a fading note, and blooms into its harmonic")
{
    auto note = [] {
        std::vector<std::vector<float>> x(1, std::vector<float>(ms(1300)));
        for (size_t i = 0; i < x[0].size(); ++i)
            x[0][i] = 0.3f * static_cast<float>(std::exp(-static_cast<double>(i) / (0.3 * kRate))
                                                * std::sin(2.0 * M_PI * 220.0 * i / kRate));
        return x;
    };
    auto held = make(kSustainerBlockDescriptor);
    held->setParam(kSustainParamSustain, 100.0f);
    held->setParam(kSustainParamFeedback, 0.0f);
    auto x = note();
    testing::runBlock(*held, x);
    // the input is 35 dB down by 1.2 s
    CHECK(testing::rms(x[0], ms(1100), ms(1200)) > 0.6 * testing::rms(x[0], ms(100), ms(200)));

    auto bloom = make(kSustainerBlockDescriptor);
    bloom->setParam(kSustainParamSustain, 100.0f);
    bloom->setParam(kSustainParamFeedback, 100.0f);
    bloom->setParam(kSustainParamBloom, 300.0f);
    auto y = note();
    testing::runBlock(*bloom, y);
    CHECK(bandAt(y[0], 220.0, ms(50), ms(150)) > 2.0 * bandAt(y[0], 440.0, ms(50), ms(150)));
    CHECK(bandAt(y[0], 440.0, ms(800), ms(1100)) > 2.0 * bandAt(y[0], 220.0, ms(800), ms(1100)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Filters

TEST_CASE("robot filter: Loop 8 repeats every eight steps, Random doesn't")
{
    // the level of each 50 ms step of a low-passed sawtooth follows the cutoff
    auto steps = [](float source) {
        auto block = make(kRobotFilterBlockDescriptor);
        block->setParam(kRobotParamSource, source);
        block->setParam(kRobotParamRate, 20.0f);
        block->setParam(kRobotParamGlide, 0.0f);
        auto x = saw(110.0, 0.2f, ms(1700));
        testing::runBlock(*block, x);
        std::vector<double> levels;
        for (int s = 1; s <= 32; ++s)
            levels.push_back(rmsDb(x[0], ms(50.0 * s + 15), ms(50.0 * s + 45)));
        return levels;
    };
    const auto loop = steps(1.0f);
    const auto random = steps(0.0f);
    double loopDiff = 0.0, randomDiff = 0.0, loopSpread = 0.0;
    for (int s = 0; s < 24; ++s)
    {
        loopDiff = std::max(loopDiff, std::fabs(loop[s] - loop[s + 8]));
        randomDiff = std::max(randomDiff, std::fabs(random[s] - random[s + 8]));
        loopSpread = std::max(loopSpread, std::fabs(loop[s] - loop[0]));
    }
    CHECK(loopDiff < 0.5);
    CHECK(loopSpread > 3.0); // it does move
    CHECK(randomDiff > 3.0);
}

TEST_CASE("robot filter's envelope source opens with louder playing")
{
    auto brightness = [](float amplitude) {
        auto block = make(kRobotFilterBlockDescriptor);
        block->setParam(kRobotParamSource, 2.0f);
        auto x = saw(110.0, amplitude, 48000);
        testing::runBlock(*block, x);
        return toneAt(x[0], 2200.0, 24000, 48000) / toneAt(x[0], 110.0, 24000, 48000);
    };
    CHECK(brightness(0.3f) > 3.0 * brightness(0.005f));
}

TEST_CASE("talk box vowels")
{
    const Formants u = talkBoxFormants(0, 0.0f), a = talkBoxFormants(0, 1.0f);
    CHECK(u.f1 == doctest::Approx(300.0f));
    CHECK(a.f1 == doctest::Approx(730.0f));
    CHECK(a.f2 == doctest::Approx(1090.0f));
    const Formants mid = talkBoxFormants(3, 0.5f); // A-E-I-O-U: the middle is I
    CHECK(mid.f2 == doctest::Approx(2290.0f));

    // on a sawtooth, "ah" has much more around its first formant than "oo"
    auto at730 = [](float position) {
        auto block = make(kTalkBoxBlockDescriptor);
        block->setParam(kTalkParamPosition, position);
        auto x = saw(110.0, 0.2f, 48000);
        testing::runBlock(*block, x);
        return toneAt(x[0], 770.0, 24000, 48000) / toneAt(x[0], 330.0, 24000, 48000);
    };
    CHECK(at730(100.0f) > 4.0 * at730(0.0f));
}

TEST_CASE("talk box envelope source opens the vowel with louder playing")
{
    auto openness = [](float amplitude) {
        auto block = make(kTalkBoxBlockDescriptor);
        block->setParam(kTalkParamSource, 1.0f);
        block->setParam(kTalkParamPosition, 0.0f);
        auto x = saw(110.0, amplitude, 48000);
        testing::runBlock(*block, x);
        return toneAt(x[0], 770.0, 24000, 48000) / toneAt(x[0], 330.0, 24000, 48000);
    };
    CHECK(openness(0.3f) > 4.0 * openness(0.003f));
}
