// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/Engine.hpp"
#include "engine/tuner/PitchDetector.hpp"
#include "engine/tuner/Tuner.hpp"

#include <cmath>
#include <random>
#include <thread>
#include <vector>

using namespace xythos;

namespace {

double cents(double f, double reference)
{
    return 1200.0 * std::log2(f / reference);
}

// A note with harmonics (falling as 1/h), slightly decaying, with some noise
std::vector<float> tone(double frequency, double sampleRate, size_t length, int harmonics, double noise,
                        double phase = 0.3, uint32_t seed = 1)
{
    std::mt19937 random(seed);
    std::normal_distribution<double> gaussian(0.0, noise);
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        double v = 0.0;
        for (int h = 1; h <= harmonics; ++h)
        {
            if (frequency * h < sampleRate / 2)
                v += std::sin(2.0 * M_PI * frequency * h * t + phase * h) / h;
        }
        out[i] = static_cast<float>(0.3 * v * std::exp(-t * 0.5) + (noise > 0 ? gaussian(random) : 0.0));
    }
    return out;
}

} // namespace

// Accuracy target (docs/Requirements.md): within 0.1 cent from a 5-string bass's low B (31 Hz) to the 24th fret of
// a guitar's high E (1319 Hz)
TEST_CASE("the pitch detector is accurate to 0.1 cent from 31 Hz to 1319 Hz")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE(rate);
        PitchDetector detector;
        uint32_t size = 1;
        while (size < 0.085 * rate)
            size <<= 1;
        detector.prepare(rate, size, 25.0, 1500.0);
        const uint32_t span = detector.maxSpan();

        double worst = 0.0;
        for (double f = 30.87; f < 1320.0; f *= 1.0293) // every half semitone from low B
        {
            CAPTURE(f);
            // a pure sine, a note rich in harmonics, and a noisy note with a weak fundamental (low notes on a bass)
            for (int variant = 0; variant < 3; ++variant)
            {
                CAPTURE(variant);
                std::vector<float> x = tone(f, rate, span, variant == 0 ? 1 : 8, variant == 2 ? 0.002 : 0.0, 0.3,
                                            static_cast<uint32_t>(f));
                if (variant == 2)
                {
                    const std::vector<float> fundamental = tone(f, rate, span, 1, 0.0);
                    for (uint32_t i = 0; i < span; ++i)
                        x[i] -= 0.85f * fundamental[i];
                }
                const PitchDetector::Result r = detector.detect(x.data(), span);
                REQUIRE(r.frequency > 0.0);
                const double error = std::fabs(cents(r.frequency, f));
                CHECK(error < 0.1);
                CHECK(r.clarity > 0.9);
                worst = std::max(worst, error);
            }
        }
        MESSAGE("worst error at " << rate << " Hz: " << worst << " cents");
    }
}

TEST_CASE("the pitch detector follows a note change within one window")
{
    const double rate = 48000.0;
    PitchDetector detector;
    detector.prepare(rate, 4096, 25.0, 1500.0);
    const uint32_t span = detector.maxSpan();
    const std::vector<float> a = tone(110.0, rate, span, 6, 0.0);
    const std::vector<float> d = tone(146.83, rate, span, 6, 0.0);
    std::vector<float> x(a.begin(), a.begin() + span - 4096);
    x.insert(x.end(), d.begin(), d.begin() + 4096);
    const PitchDetector::Result r = detector.detect(x.data(), span);
    CHECK(std::fabs(cents(r.frequency, 146.83)) < 0.5);
}

TEST_CASE("the tuner analyses pushed audio, gates silence and forgets old audio on restart")
{
    const double rate = 48000.0;
    Tuner tuner;
    tuner.prepare(rate);
    CHECK(tuner.analyze().frequency == 0.0);

    const std::vector<float> note = tone(82.41, rate, 32768, 6, 0.0);
    for (size_t i = 0; i + 256 <= note.size(); i += 256)
        tuner.push(note.data() + i, nullptr, 256);
    const PitchDetector::Result r = tuner.analyze();
    CHECK(std::fabs(cents(r.frequency, 82.41)) < 0.1);

    // stereo input is analysed as the sum; a very quiet note still works
    std::vector<float> quiet(note);
    for (float& v : quiet)
        v *= 0.002f; // about -60 dBFS
    tuner.restart();
    CHECK(tuner.analyze().frequency == 0.0);
    for (size_t i = 0; i + 256 <= quiet.size(); i += 256)
        tuner.push(quiet.data() + i, quiet.data() + i, 256);
    CHECK(std::fabs(cents(tuner.analyze().frequency, 82.41)) < 0.1);

    const std::vector<float> silence(4096 * 4, 0.0f);
    tuner.push(silence.data(), nullptr, static_cast<uint32_t>(silence.size()));
    CHECK(tuner.analyze().frequency == 0.0);
}

namespace {

// Pushes `signal` through a tuner in 256-sample buffers, analysing every 4 buffers (about 20 ms, as the plugin
// does), and returns the worst error of any reading and how many readings there were
std::pair<double, int> track(const std::vector<float>& signal, double frequency, double rate)
{
    Tuner tuner;
    tuner.prepare(rate);
    double worst = 0.0;
    int readings = 0;
    for (size_t b = 0; (b + 1) * 256 <= signal.size(); ++b)
    {
        tuner.push(signal.data() + b * 256, nullptr, 256);
        if (b % 4 != 3)
            continue;
        const PitchDetector::Result r = tuner.analyze();
        if (r.frequency > 0.0)
        {
            worst = std::max(worst, std::fabs(cents(r.frequency, frequency)));
            ++readings;
        }
    }
    return { worst, readings };
}

} // namespace

TEST_CASE("the tuner stays accurate through a note's start and end: unsteady audio gives no reading")
{
    const double rate = 48000.0;

    SUBCASE("a gated tone: 1% attack, 10% linear fade, 50 ms gap, repeated")
    {
        const double f = 111.0;
        const int toneLength = 45600, attack = toneLength / 100, decay = toneLength / 10;
        std::vector<float> x(48000 * 3, 0.0f);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const int j = static_cast<int>(i % 48000);
            if (j >= toneLength)
                continue;
            double amp = 0.3;
            if (j < attack)
                amp *= j / static_cast<double>(attack);
            if (j >= toneLength - decay)
                amp *= (toneLength - j) / static_cast<double>(decay);
            x[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * f / rate * j));
        }
        const auto [worst, readings] = track(x, f, rate);
        CHECK(readings > 100);
        CHECK(worst < 0.1);
    }

    SUBCASE("a plucked note: harmonics decaying faster the higher they are, from silence")
    {
        const double f = 146.83; // D3
        std::vector<float> x(48000 * 3, 0.0f);
        for (size_t i = 4800; i < x.size(); ++i)
        {
            const double t = (i - 4800) / rate;
            double v = 0.0;
            for (int h = 1; h <= 12; ++h)
                v += std::exp(-t * (0.8 + 0.6 * h)) * std::sin(2.0 * M_PI * f * h * t + h) / h;
            x[i] = static_cast<float>(0.5 * std::min(1.0, t / 0.002) * v);
        }
        const auto [worst, readings] = track(x, f, rate);
        CHECK(readings > 100);
        CHECK(worst < 0.1);
    }
}

TEST_CASE("engine: the tuner reads the dry input, and mutes the guitar while on")
{
    const double rate = 48000.0;
    const uint32_t buffer = 256;
    Engine engine { testing::testRegistry() };
    engine.prepare(rate, buffer);
    engine.setParameter(kParamLimiter, 0.0f);

    const std::vector<float> note = tone(196.0, rate, 48000, 6, 0.0);
    std::vector<float> outL(buffer), outR(buffer);
    auto run = [&](size_t& pos, int buffers) {
        for (int b = 0; b < buffers; ++b, pos += buffer)
        {
            const float* in[2] = { note.data() + pos % (note.size() - buffer), note.data() + pos % (note.size() - buffer) };
            float* out[2] = { outL.data(), outR.data() };
            engine.process(in, out, buffer);
        }
    };
    auto peak = [&] {
        float p = 0.0f;
        for (float v : outL)
            p = std::max(p, std::fabs(v));
        return p;
    };

    size_t pos = 0;
    run(pos, 40);
    engine.analyzeTuner();
    CHECK(engine.getParameter(kParamTunerFrequency) == 0.0f); // off
    CHECK(peak() > 0.1f);

    engine.setParameter(kParamTuner, 1.0f);
    {
        testing::AllocationGuard guard;
        run(pos, 80);
        CHECK(guard.count() == 0);
    }
    engine.analyzeTuner();
    CHECK(std::fabs(cents(engine.getParameter(kParamTunerFrequency), 196.0)) < 0.1);
    CHECK(engine.getParameter(kParamTunerClarity) > 0.9f);
    CHECK(peak() == 0.0f); // muted

    // muted in global bypass too
    engine.setParameter(kParamBypass, 1.0f);
    run(pos, 10);
    CHECK(peak() == 0.0f);
    engine.setParameter(kParamBypass, 0.0f);

    engine.setParameter(kParamTunerMute, 0.0f);
    run(pos, 10);
    CHECK(peak() > 0.1f);
}

TEST_CASE("engine: tuner analysis on another thread while audio runs")
{
    const double rate = 48000.0;
    Engine engine { testing::testRegistry() };
    engine.prepare(rate, 128);
    engine.setParameter(kParamTuner, 1.0f);
    const std::vector<float> note = tone(329.63, rate, 48000, 4, 0.0);

    std::atomic<bool> running { true };
    std::thread analyser([&] {
        while (running)
            engine.analyzeTuner();
    });
    std::vector<float> out(128);
    for (size_t pos = 0; pos + 128 <= note.size(); pos += 128)
    {
        const float* in[2] = { note.data() + pos, note.data() + pos };
        float* outs[2] = { out.data(), out.data() };
        engine.process(in, outs, 128);
    }
    running = false;
    analyser.join();
    engine.analyzeTuner();
    CHECK(std::fabs(cents(engine.getParameter(kParamTunerFrequency), 329.63)) < 0.1);
}
