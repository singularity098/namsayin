// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// Behaviour tests for the synths in SynthBlocks (the original Synth block is tested in PedalTests.cpp)

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/blocks/PolyChipBlock.hpp"
#include "engine/blocks/SynthBlocks.hpp"
#include "engine/dsp/Biquad.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace namsayin;

namespace {

constexpr double kRate = 48000.0;

// A guitar-like tone: a fundamental and strong harmonics, steady, or fading if `decaySeconds` > 0
std::vector<std::vector<float>> note(double hz, size_t length, int channels = 1, double decaySeconds = 0.0)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length));
    for (auto& ch : x)
    {
        for (size_t i = 0; i < length; ++i)
        {
            const double t = static_cast<double>(i) / kRate;
            const double fade = decaySeconds > 0.0 ? std::exp(-t / decaySeconds) : 1.0;
            ch[i] = static_cast<float>(fade * (0.2 * std::sin(2 * M_PI * hz * t) + 0.15 * std::sin(2 * M_PI * 2 * hz * t + 1.0)
                                               + 0.1 * std::sin(2 * M_PI * 3 * hz * t + 2.0)));
        }
    }
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

// Level in a band around `hz` (+-3%): for the detuned saws
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

// How much of x[from, to) is above `hz` (RMS after a high-pass, against the whole)
double highShare(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    namsayin::Biquad filter[2];
    for (auto& f : filter)
        f.setHighPass(kRate, hz);
    std::vector<float> high(x);
    for (float& v : high)
        v = filter[1].process(filter[0].process(v));
    return testing::rms(high, from, to) / testing::rms(x, from, to);
}

size_t ms(double milliseconds)
{
    return static_cast<size_t>(milliseconds / 1000.0 * kRate);
}

} // namespace

TEST_CASE("every synth plays the note you play, and an octave down when asked")
{
    for (const BlockDescriptor* type : { &kSuperSawBlockDescriptor, &kFmSynthBlockDescriptor, &kFoldSynthBlockDescriptor,
                                         &kAcidSynthBlockDescriptor, &kStringSynthBlockDescriptor,
                                         &kChipSynthBlockDescriptor, &kPolyChipBlockDescriptor })
    {
        const std::string typeId = type->typeId;
        CAPTURE(typeId);
        // the octave knob is at the same place in each: find it by name
        int octave = -1;
        for (int p = 0; p < type->numParams; ++p)
            if (std::string(type->params[p].name) == "Octave")
                octave = p;
        REQUIRE(octave >= 0);
        for (const float choice : { 2.0f, 1.0f }) // 0, -1
        {
            CAPTURE(choice);
            auto block = make(*type);
            block->setParam(octave, choice);
            auto x = note(196.0, 48000);
            testing::runBlock(*block, x);
            const double hz = choice == 2.0f ? 196.0 : 98.0;
            const double played = bandAt(x[0], hz, 24000, 48000);
            CHECK(played > 0.01);
            // nothing much a semitone either side: it's on the note
            CHECK(played > 3.0 * toneAt(x[0], hz * std::exp2(-1.0 / 12.0), 24000, 48000));
            CHECK(played > 3.0 * toneAt(x[0], hz * std::exp2(1.0 / 12.0), 24000, 48000));
        }
    }
}

TEST_CASE("super saw: detuned saws, spread in stereo")
{
    auto block = make(kSuperSawBlockDescriptor);
    block->setParam(kSuperSawParamCutoff, 12000.0f);
    block->setParam(kSuperSawParamWidth, 100.0f);
    auto x = note(196.0, 48000, 2);
    testing::runBlock(*block, x);
    double difference = 0.0;
    for (size_t i = 24000; i < 48000; ++i)
        difference = std::max(difference, static_cast<double>(std::fabs(x[0][i] - x[1][i])));
    CHECK(difference > 0.02);

    // no detune and no width: one saw, the same both sides, with all its harmonics
    auto plain = make(kSuperSawBlockDescriptor);
    plain->setParam(kSuperSawParamCutoff, 12000.0f);
    plain->setParam(kSuperSawParamDetune, 0.0f);
    plain->setParam(kSuperSawParamWidth, 0.0f);
    auto y = note(196.0, 48000, 2);
    testing::runBlock(*plain, y);
    for (size_t i = 24000; i < 48000; i += 101)
        CHECK(y[0][i] == doctest::Approx(y[1][i]).epsilon(1e-5));
    const double fundamental = toneAt(y[0], 196.0, 24000, 48000);
    CHECK(toneAt(y[0], 392.0, 24000, 48000) == doctest::Approx(fundamental / 2.0).epsilon(0.15));
}

TEST_CASE("FM synth: the ratio sets the harmonics, and the brightness decays after the pick")
{
    // 1:2 makes odd harmonics only; 1:1 all of them
    auto odd = make(kFmSynthBlockDescriptor);
    odd->setParam(kFmParamRatio, 2.0f);
    odd->setParam(kFmParamSustain, 100.0f);
    auto x = note(196.0, 48000);
    testing::runBlock(*odd, x);
    const double fundamental = toneAt(x[0], 196.0, 24000, 48000);
    CHECK(toneAt(x[0], 588.0, 24000, 48000) > 0.2 * fundamental);
    CHECK(toneAt(x[0], 392.0, 24000, 48000) < 0.02 * fundamental);

    auto all = make(kFmSynthBlockDescriptor);
    all->setParam(kFmParamRatio, 1.0f);
    all->setParam(kFmParamSustain, 100.0f);
    auto y = note(196.0, 48000);
    testing::runBlock(*all, y);
    CHECK(toneAt(y[0], 392.0, 24000, 48000) > 0.2 * toneAt(y[0], 196.0, 24000, 48000));

    // Index 0: a pure sine
    auto pure = make(kFmSynthBlockDescriptor);
    pure->setParam(kFmParamIndex, 0.0f);
    auto z = note(196.0, 48000);
    testing::runBlock(*pure, z);
    CHECK(toneAt(z[0], 392.0, 24000, 48000) < 0.01 * toneAt(z[0], 196.0, 24000, 48000));

    // brightness falls to Sustain (0) over Decay (100 ms)
    auto pluck = make(kFmSynthBlockDescriptor);
    pluck->setParam(kFmParamDecay, 100.0f);
    pluck->setParam(kFmParamSustain, 0.0f);
    auto p = note(196.0, 48000);
    testing::runBlock(*pluck, p);
    const double early = toneAt(p[0], 392.0, ms(30), ms(90)) / toneAt(p[0], 196.0, ms(30), ms(90));
    const double late = toneAt(p[0], 392.0, ms(600), ms(900)) / toneAt(p[0], 196.0, ms(600), ms(900));
    CHECK(late < 0.1 * early);
}

TEST_CASE("fold synth folds harder with Fold, and Pluck dies away")
{
    auto brightness = [](float fold) {
        auto block = make(kFoldSynthBlockDescriptor);
        block->setParam(kFoldParamFold, fold);
        block->setParam(kFoldParamEnv, 0.0f);
        block->setParam(kFoldParamTone, 16000.0f);
        auto x = note(196.0, 48000);
        testing::runBlock(*block, x);
        return highShare(x[0], 1500.0, 24000, 48000);
    };
    CHECK(brightness(100.0f) > 5.0 * brightness(0.0f));

    auto pluck = make(kFoldSynthBlockDescriptor);
    pluck->setParam(kFoldParamGate, 1.0f);
    pluck->setParam(kFoldParamDecay, 100.0f);
    auto x = note(196.0, 48000); // a steady note: only the ping makes it fade
    testing::runBlock(*pluck, x);
    CHECK(testing::rms(x[0], ms(30), ms(80)) > 0.01);
    CHECK(testing::rms(x[0], ms(700), ms(900)) < 0.01 * testing::rms(x[0], ms(30), ms(80)));
}

TEST_CASE("acid synth's filter snaps open on the pick and closes")
{
    auto block = make(kAcidSynthBlockDescriptor);
    block->setParam(kAcidParamOctave, 2.0f);
    block->setParam(kAcidParamDecay, 100.0f);
    block->setParam(kAcidParamEnvMod, 100.0f);
    auto x = note(110.0, 48000);
    testing::runBlock(*block, x);
    CHECK(highShare(x[0], 1500.0, ms(20), ms(80)) > 3.0 * highShare(x[0], 1500.0, ms(600), ms(900)));
}

TEST_CASE("string machine swells in, rings on, and spreads in stereo")
{
    auto block = make(kStringSynthBlockDescriptor); // 300 ms attack, 1.2 s release
    auto x = note(196.0, 72000, 2);
    for (auto& ch : x)
        std::fill(ch.begin() + 48000, ch.end(), 0.0f);
    testing::runBlock(*block, x);
    const double full = testing::rms(x[0], ms(800), ms(1000));
    CHECK(full > 0.02);
    CHECK(testing::rms(x[0], 0, ms(40)) < 0.25 * full);
    // the input stops at 1 s: still ringing 300 ms later
    CHECK(testing::rms(x[0], ms(1250), ms(1350)) > 0.3 * full);
    double difference = 0.0;
    for (size_t i = 24000; i < 48000; ++i)
        difference = std::max(difference, static_cast<double>(std::fabs(x[0][i] - x[1][i])));
    CHECK(difference > 0.005);
}

TEST_CASE("chip synth snaps to semitones and arpeggiates")
{
    // a slightly sharp A (445 Hz) plays as A 440 when quantized
    auto block = make(kChipSynthBlockDescriptor);
    block->setParam(kChipParamVibrato, 0.0f);
    block->setParam(kChipParamWave, 2.0f); // a square: no even harmonics of its own
    auto x = note(445.0, 48000);
    testing::runBlock(*block, x);
    CHECK(toneAt(x[0], 440.0, 24000, 48000) > 4.0 * toneAt(x[0], 445.0, 24000, 48000));
    CHECK(toneAt(x[0], 880.0, 24000, 48000) < 0.1 * toneAt(x[0], 440.0, 24000, 48000));

    // the octave arpeggio adds the octave
    block->setParam(kChipParamArp, 1.0f);
    auto y = note(445.0, 48000);
    testing::runBlock(*block, y);
    CHECK(toneAt(y[0], 880.0, 24000, 48000) > 0.3 * toneAt(y[0], 440.0, 24000, 48000));
}

// ---------------------------------------------------------------------------------------------------------------------
// Poly Chip

namespace {

// Several guitar-like notes at once (with the note() helper's harmonics), or pure sines
std::vector<std::vector<float>> chord(const std::vector<double>& notes, size_t length, bool harmonics = true)
{
    std::vector<std::vector<float>> x(1, std::vector<float>(length, 0.0f));
    for (const double hz : notes)
    {
        const auto one = note(hz, length);
        for (size_t i = 0; i < length; ++i)
        {
            const double t = static_cast<double>(i) / kRate;
            x[0][i] += harmonics ? one[0][i] : static_cast<float>(0.2 * std::sin(2 * M_PI * hz * t));
        }
    }
    return x;
}

std::unique_ptr<Block> polyChip()
{
    auto block = make(kPolyChipBlockDescriptor);
    block->setParam(kPolyChipParamWave, 2.0f); // a square: no even harmonics of its own
    block->setParam(kPolyChipParamCrush, 0.0f);
    return block;
}

double semitone(int midi)
{
    return 440.0 * std::exp2((midi - 69) / 12.0);
}

} // namespace

TEST_CASE("poly chip plays one note, without the string's harmonics")
{
    for (const int midi : { 40, 55, 69, 81 }) // E2, G3, A4, A5
    {
        CAPTURE(midi);
        const double hz = semitone(midi);
        auto block = polyChip();
        auto x = note(hz * std::exp2(0.2 / 12.0), 48000); // 20 cents sharp: snapped to the semitone
        testing::runBlock(*block, x);
        const double played = toneAt(x[0], hz, 24000, 48000);
        CHECK(played > 0.05);
        if (midi >= 69) // a 0.5 s window can't tell 20 cents apart on low notes
            CHECK(played > 4.0 * toneAt(x[0], hz * std::exp2(0.2 / 12.0), 24000, 48000));
        CHECK(toneAt(x[0], 2.0 * hz, 24000, 48000) < 0.1 * played); // no octave voice from the 2nd harmonic
        CHECK(played > 5.0 * toneAt(x[0], hz * std::exp2(-1.0 / 12.0), 24000, 48000));
        CHECK(played > 5.0 * toneAt(x[0], hz * std::exp2(1.0 / 12.0), 24000, 48000));
    }
}

TEST_CASE("poly chip plays every note of a chord")
{
    // an open E major chord, low strings: E2 B2 E3 G#3 B3 (the upper E and B are octaves of lower notes, so they're
    // heard as harmonics); and a C major triad higher up
    for (const auto& notes : { std::vector<int> { 40, 47, 56 }, std::vector<int> { 60, 64, 67 } })
    {
        auto block = polyChip();
        std::vector<double> hz;
        for (const int midi : notes)
            hz.push_back(semitone(midi));
        auto x = chord(hz, 48000);
        testing::runBlock(*block, x);
        double weakest = 1.0;
        for (const int midi : notes)
        {
            CAPTURE(midi);
            const double level = toneAt(x[0], semitone(midi), 24000, 48000);
            CHECK(level > 0.04);
            weakest = std::min(weakest, level);
        }
        // not a note of the chord, nor a semitone off one
        for (const int off : { -1, 1, 2 })
            CHECK(toneAt(x[0], semitone(notes[1] + off), 24000, 48000) < 0.2 * weakest);
    }
}

TEST_CASE("poly chip voices: 1 plays only the loudest note")
{
    auto block = polyChip();
    block->setParam(kPolyChipParamVoices, 1.0f);
    std::vector<std::vector<float>> x(1, std::vector<float>(48000, 0.0f));
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate;
        x[0][i] = static_cast<float>(0.1 * std::sin(2 * M_PI * semitone(48) * t) + 0.3 * std::sin(2 * M_PI * semitone(55) * t));
    }
    testing::runBlock(*block, x);
    const double loud = toneAt(x[0], semitone(55), 24000, 48000);
    CHECK(loud > 0.05);
    CHECK(toneAt(x[0], semitone(48), 24000, 48000) < 0.05 * loud);
}

TEST_CASE("poly chip arpeggiates the chord, one note at a time")
{
    const std::vector<int> notes { 48, 55, 64 }; // C3 G3 E4: far enough apart to tell in short windows
    auto block = polyChip();
    block->setParam(kPolyChipParamArp, static_cast<float>(PolyChipArp::Up));
    block->setParam(kPolyChipParamArpRate, 10.0f);
    auto x = chord({ semitone(48), semitone(55), semitone(64) }, 96000, false);
    testing::runBlock(*block, x);
    // each note gets its turn
    for (const int midi : notes)
        CHECK(toneAt(x[0], semitone(midi), 24000, 96000) > 0.02);
    // and in most short windows one note dominates
    int clear = 0, windows = 0;
    for (size_t from = 24000; from + 2400 <= 96000; from += 2400, ++windows)
    {
        std::vector<double> levels;
        for (const int midi : notes)
            levels.push_back(toneAt(x[0], semitone(midi), from, from + 2400));
        std::sort(levels.begin(), levels.end());
        if (levels[2] > 3.0 * levels[1])
            ++clear;
    }
    CHECK(clear >= windows / 2);
}

TEST_CASE("poly chip falls silent after the chord")
{
    auto block = polyChip();
    auto x = chord({ semitone(52), semitone(56), semitone(59) }, 48000);
    std::fill(x[0].begin() + 24000, x[0].end(), 0.0f);
    testing::runBlock(*block, x);
    CHECK(testing::rms(x[0], 12000, 24000) > 0.05);
    CHECK(testing::rms(x[0], 24000 + ms(400), 48000) < 1e-4);
}
