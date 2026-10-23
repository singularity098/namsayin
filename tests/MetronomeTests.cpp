// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/Engine.hpp"
#include "engine/metronome/Metronome.hpp"

#include <cmath>
#include <vector>

using namespace namsayin;

namespace {

constexpr double kRate = 48000.0;

Metronome::Settings settings(double bpm)
{
    Metronome::Settings s;
    s.on = true;
    s.bpm = bpm;
    s.levels.fill(2); // all normal: the same sample for every beat, so onsets compare exactly
    s.volume = 1.0f;
    s.subVolume = 1.0f;
    return s;
}

// Renders `length` samples in buffers of varying sizes
std::vector<float> renderFree(Metronome& metronome, const Metronome::Settings& s, size_t length)
{
    std::vector<float> out(length, 0.0f);
    static const uint32_t sizes[] = { 64, 1, 333, 256, 1000, 17 };
    size_t pos = 0;
    for (int i = 0; pos < length; ++i)
    {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(sizes[i % 6], length - pos));
        metronome.process(out.data() + pos, n, s);
        pos += n;
    }
    return out;
}

// Where clicks start: the first non-zero sample after at least `gap` silent ones
std::vector<size_t> onsets(const std::vector<float>& x, size_t gap = 200)
{
    std::vector<size_t> result;
    size_t silent = gap;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (x[i] != 0.0f)
        {
            if (silent >= gap)
                result.push_back(i);
            silent = 0;
        }
        else
        {
            ++silent;
        }
    }
    return result;
}

float peak(const std::vector<float>& x, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < to && i < x.size(); ++i)
        p = std::max(p, std::fabs(x[i]));
    return p;
}

// Offset from a click's scheduled time to its first non-zero sample
size_t onsetDelay(const Metronome::Settings& s)
{
    Metronome metronome;
    metronome.prepare(kRate);
    const std::vector<float> out = renderFree(metronome, s, 4800);
    return onsets(out).at(0);
}

} // namespace

TEST_CASE("metronome: clicks land on the exact sample, without drift, whatever the buffer sizes")
{
    for (const double bpm : { 120.0, 137.0, 20.0, 300.0 })
    {
        CAPTURE(bpm);
        const Metronome::Settings s = settings(bpm);
        const size_t delay = onsetDelay(s);
        Metronome metronome;
        metronome.prepare(kRate);
        const double beat = 60.0 / bpm * kRate;
        const std::vector<float> out = renderFree(metronome, s, static_cast<size_t>(beat * 40 + 100));
        const std::vector<size_t> found = onsets(out);
        REQUIRE(found.size() == 41);
        for (size_t k = 0; k < found.size(); ++k)
            CHECK(found[k] == static_cast<size_t>(std::llround(k * beat)) + delay);
    }
}

TEST_CASE("metronome: subdivisions")
{
    Metronome::Settings s = settings(100.0); // 28800 samples per beat
    const size_t delay = onsetDelay(s);

    s.subdivision = MetroSubdivision::Triplets;
    Metronome metronome;
    metronome.prepare(kRate);
    std::vector<size_t> found = onsets(renderFree(metronome, s, 28800 * 2));
    CHECK(found == std::vector<size_t> { delay, 9600 + delay, 19200 + delay, 28800 + delay, 38400 + delay,
                                         48000 + delay });

    s.subdivision = MetroSubdivision::Dotted;
    metronome.prepare(kRate);
    found = onsets(renderFree(metronome, s, 28800));
    CHECK(found == std::vector<size_t> { delay, 21600 + delay });

    // all eleven subdivisions have the right number of clicks per beat
    for (int i = 0; i < static_cast<int>(MetroSubdivision::Count); ++i)
    {
        s.subdivision = static_cast<MetroSubdivision>(i);
        metronome.prepare(kRate);
        CHECK(onsets(renderFree(metronome, s, 28800)).size() == Metronome::subdivisionOffsets(s.subdivision).size());
    }
}

TEST_CASE("metronome: accents, soft and muted beats, volumes and sounds")
{
    Metronome::Settings s = settings(120.0); // 24000 samples per beat
    s.levels = { 3, 0, 1, 2 };
    Metronome metronome;
    metronome.prepare(kRate);
    const std::vector<float> out = renderFree(metronome, s, 24000 * 4);
    CHECK(peak(out, 0, 4000) > 0.0f);                    // accent (the high sample)
    CHECK(peak(out, 24000, 48000) == 0.0f);              // muted
    const float soft = peak(out, 48000, 52000), normal = peak(out, 72000, 76000);
    CHECK(soft / normal == doctest::Approx(0.4 / 0.8).epsilon(0.001));

    // subdivision volume is separate; volume scales everything
    s.levels.fill(2);
    s.subdivision = MetroSubdivision::Eighths;
    s.subVolume = 0.5f;
    s.volume = 0.5f;
    metronome.prepare(kRate);
    const std::vector<float> sub = renderFree(metronome, s, 24000);
    CHECK(peak(sub, 12000, 16000) / peak(sub, 0, 4000) == doctest::Approx(0.5 * 0.75 / 0.8).epsilon(0.001));
    CHECK(peak(sub, 0, 4000) == doctest::Approx(0.5 * peak(out, 72000, 76000)).epsilon(0.001));

    for (const MetroSound sound : { MetroSound::Classic, MetroSound::Digital, MetroSound::Beep })
    {
        s.sound = sound;
        metronome.prepare(kRate);
        CHECK(peak(renderFree(metronome, s, 4000), 0, 4000) > 0.05f);
    }
}

TEST_CASE("metronome: stopping lets the click play out; starting again begins at beat 1")
{
    Metronome::Settings s = settings(120.0);
    Metronome metronome;
    metronome.prepare(kRate);
    std::vector<float> out(48000, 0.0f);
    metronome.process(out.data(), 100, s);
    CHECK(metronome.status().beat == 0);
    s.on = false;
    metronome.process(out.data() + 100, 47900, s);
    CHECK(metronome.status().beat == -1);
    CHECK(peak(out, 100, 400) > 0.0f);          // the click went on
    CHECK(onsets(out).size() == 1);             // and no new one started

    s.on = true;
    s.levels[0] = 3;
    std::fill(out.begin(), out.end(), 0.0f);
    metronome.process(out.data(), 64, s);
    CHECK(metronome.status().beat == 0);
    CHECK(metronome.status().bar == 0);
}

TEST_CASE("metronome: step trainer, gradual trainer and gap trainer")
{
    SUBCASE("step: +5 BPM every bar from 100 to 110, then it stays")
    {
        Metronome::Settings s = settings(60.0);
        s.beats = 2;
        s.trainer = MetroTrainer::Step;
        s.trainerStart = 100.0;
        s.trainerEnd = 110.0;
        s.trainerStep = 5.0;
        s.trainerEvery = 1;
        Metronome metronome;
        metronome.prepare(kRate);
        std::vector<double> tempos;
        std::vector<float> out(64);
        uint32_t count = 0;
        while (tempos.size() < 8)
        {
            std::fill(out.begin(), out.end(), 0.0f);
            metronome.process(out.data(), 64, s);
            if (metronome.status().beatCount != count)
            {
                count = metronome.status().beatCount;
                tempos.push_back(metronome.status().bpm);
            }
        }
        CHECK(tempos == std::vector<double> { 100, 100, 105, 105, 110, 110, 110, 110 });
    }

    SUBCASE("gradual: 60 to 120 BPM over 2 bars, smoothly; also slowing down")
    {
        for (const bool down : { false, true })
        {
            Metronome::Settings s = settings(100.0);
            s.trainer = MetroTrainer::Gradual;
            s.trainerStart = down ? 120.0 : 60.0;
            s.trainerEnd = down ? 60.0 : 120.0;
            s.trainerBars = 2;
            Metronome metronome;
            metronome.prepare(kRate);
            std::vector<double> tempos;
            std::vector<float> out(64);
            uint32_t count = 0;
            while (tempos.size() < 10)
            {
                metronome.process(out.data(), 64, s);
                if (metronome.status().beatCount != count)
                {
                    count = metronome.status().beatCount;
                    tempos.push_back(metronome.status().bpm);
                }
            }
            for (size_t i = 0; i < tempos.size(); ++i)
            {
                const double expected = 60.0 + 60.0 * std::min(1.0, i / 8.0);
                CHECK(tempos[i] == doctest::Approx(down ? 180.0 - expected : expected));
            }
        }
    }

    SUBCASE("gap: play 1 bar, mute 2")
    {
        Metronome::Settings s = settings(240.0); // 12000 samples per beat
        s.beats = 2;
        s.gap = true;
        s.gapPlay = 1;
        s.gapMute = 2;
        Metronome metronome;
        metronome.prepare(kRate);
        const std::vector<float> out = renderFree(metronome, s, 24000 * 6);
        CHECK(peak(out, 0, 24000) > 0.0f);
        CHECK(peak(out, 24000 + 4000, 72000) == 0.0f); // bars 2 and 3 (after bar 1's click has faded)
        CHECK(peak(out, 72000, 96000) > 0.0f);
    }
}

TEST_CASE("metronome: DAW sync follows the host's tempo, meter and position")
{
    Metronome::Settings s = settings(100.0);
    s.sync = true;
    s.levels = { 3, 2, 2, 2, 2, 2 };
    s.subdivision = MetroSubdivision::Eighths;
    const size_t delay = onsetDelay(settings(100.0));

    Transport transport;
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 150.0; // 19200 samples per beat
    transport.beatsPerBar = 3;
    transport.beat = 2.75; // a quarter beat before bar 2

    Metronome metronome;
    metronome.prepare(kRate);
    const uint32_t buffer = 500;
    std::vector<float> out(19200 * 4, 0.0f);
    std::vector<int> beats;
    for (size_t pos = 0; pos + buffer <= out.size(); pos += buffer)
    {
        metronome.process(out.data() + pos, buffer, s, &transport);
        transport.beat += buffer * transport.bpm / 60.0 / kRate;
        if (beats.empty() || metronome.status().beat != beats.back())
            beats.push_back(metronome.status().beat);
    }
    // beat 3 is at 4800 samples (an accent: bar 2 starts), then eighths every 9600 samples
    const std::vector<size_t> found = onsets(out);
    REQUIRE(found.size() >= 6);
    CHECK(found[0] >= 4800);
    CHECK(found[0] <= 4800 + delay + 50); // the accent is a different sample, with its own delay
    CHECK(beats == std::vector<int> { -1, 0, 1, 2, 0 });
    for (size_t k = 1; k < 6; ++k)
        CHECK(found[k] == 4800 + 9600 * k + delay);
    CHECK(metronome.status().bar == 2); // beat 6 in 3/4

    // stopped transport: silence
    transport.playing = false;
    std::vector<float> quiet(19200, 0.0f);
    metronome.process(quiet.data(), 19200, s, &transport);
    CHECK(metronome.status().beat == -1);

    // a jump back (a loop) plays the beat at the new position
    transport.playing = true;
    transport.beat = 1.0;
    std::fill(quiet.begin(), quiet.end(), 0.0f);
    metronome.process(quiet.data(), 1000, s, &transport);
    CHECK(metronome.status().beat == 1);
    CHECK(peak(quiet, 0, 1000) > 0.0f);
}

TEST_CASE("metronome: no click is played twice or missed at buffer boundaries in DAW sync")
{
    Metronome::Settings s = settings(100.0);
    s.sync = true;
    s.subdivision = MetroSubdivision::Sixteenths;
    Transport transport;
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 123.0;
    transport.beat = 0.0;
    Metronome metronome;
    metronome.prepare(kRate);
    std::vector<float> out(64);
    int clicks = 0;
    uint32_t count = 0;
    const double rate = transport.bpm / 60.0 / kRate;
    for (int b = 0; b < 3000; ++b)
    {
        metronome.process(out.data(), 64, s, &transport);
        // the host's position, with a little rounding error
        transport.beat += 64 * rate + (b % 2 ? 1e-9 : -1e-9);
        if (metronome.status().beatCount != count)
        {
            count = metronome.status().beatCount;
            ++clicks;
        }
    }
    CHECK(clicks == static_cast<int>(std::ceil(3000 * 64 * rate)));
}

TEST_CASE("engine: the metronome is mixed in before the limiter, stays audible with the tuner's mute, and reports")
{
    Engine engine { testing::testRegistry() };
    engine.prepare(kRate, 256);
    std::vector<float> in(256, 0.0f), outL(256), outR(256);
    const float* ins[2] = { in.data(), in.data() };
    float* outs[2] = { outL.data(), outR.data() };

    engine.setParameter(kParamMetronome, 1.0f);
    engine.setParameter(kParamTuner, 1.0f); // mutes the guitar, not the metronome
    float loudest = 0.0f;
    {
        testing::AllocationGuard guard;
        for (int i = 0; i < 100; ++i)
        {
            engine.process(ins, outs, 256);
            loudest = std::max(loudest, std::fabs(outR[0]) + std::fabs(outR[100]));
        }
        CHECK(guard.count() == 0);
    }
    CHECK(loudest > 0.0f);
    CHECK(engine.getParameter(kParamMetroBeat) >= 0.0f);
    CHECK(engine.getParameter(kParamMetroBeatCount) >= 1.0f);
    CHECK(engine.getParameter(kParamMetroLiveTempo) == 100.0f);

    engine.setParameter(kParamMetronome, 0.0f);
    for (int i = 0; i < 20; ++i)
        engine.process(ins, outs, 256);
    CHECK(engine.getParameter(kParamMetroBeat) == -1.0f);
    CHECK(peak(outL, 0, 256) == 0.0f);
}

TEST_CASE("metronome: the Classic click is generated: 1500 Hz accent, 1000 Hz beat, a 50 ms decaying tone")
{
    Metronome::Settings s = settings(120.0);
    s.levels = { 3, 2, 2, 2 };
    s.sound = MetroSound::Classic;
    Metronome metronome;
    metronome.prepare(kRate);
    const std::vector<float> out = renderFree(metronome, s, 48000);
    // the strength of one frequency over a stretch (single-bin DFT)
    const auto tone = [&](size_t from, size_t length, double hz) {
        double re = 0.0, im = 0.0;
        for (size_t i = from; i < from + length; ++i)
        {
            re += out[i] * std::cos(2.0 * M_PI * hz * static_cast<double>(i) / kRate);
            im += out[i] * std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kRate);
        }
        return 2.0 * std::hypot(re, im) / static_cast<double>(length);
    };
    const size_t beat = 24000;
    const size_t span = static_cast<size_t>(0.02 * kRate); // 20 ms: whole cycles of 1000, 1500 and 2000 Hz
    CHECK(tone(0, span, 1500.0) > 5.0 * tone(0, span, 1000.0));
    CHECK(tone(beat, span, 1000.0) > 5.0 * tone(beat, span, 1500.0));
    // the 2nd harmonic at 0.3 of the note (-10.5 dB)
    CHECK(tone(beat, span, 2000.0) / tone(beat, span, 1000.0) == doctest::Approx(0.3).epsilon(0.05));
    // decays with a 50 ms time constant, and ends after 50 ms
    const double early = tone(beat, span, 1000.0), later = tone(beat + span, span, 1000.0);
    CHECK(later / early == doctest::Approx(std::exp(-0.02 / 0.05)).epsilon(0.05));
    CHECK(peak(out, beat + static_cast<size_t>(0.051 * kRate), 2 * beat) == 0.0f);
    // it starts softly: no step at the first sample
    CHECK(std::fabs(out[beat]) < 0.01f);
}
