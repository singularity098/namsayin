// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "engine/Block.hpp"
#include "engine/dsp/Limiter.hpp"
#include "engine/dsp/Meter.hpp"
#include "engine/dsp/ToneStack.hpp"

#include <cmath>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> sine(double freq, uint32_t frames, float amplitude = 0.5f)
{
    std::vector<float> x(frames);
    for (uint32_t i = 0; i < frames; ++i)
        x[i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * freq * i / kRate));
    return x;
}

float peak(const std::vector<float>& x, size_t from)
{
    float p = 0.0f;
    for (size_t i = from; i < x.size(); ++i)
        p = std::fmax(p, std::fabs(x[i]));
    return p;
}

float toneStackGainDb(double freq, float bass, float middle, float treble)
{
    ToneStack tone;
    tone.setGains(bass, middle, treble);
    tone.prepare(kRate);
    std::vector<float> x = sine(freq, 48000);
    std::vector<float> y = x;
    float* io[2] = { y.data(), y.data() };
    tone.process(io, 1, static_cast<uint32_t>(y.size()));
    return 20.0f * std::log10(peak(y, 24000) / peak(x, 24000));
}

} // namespace

TEST_CASE("ParamSpec maps normalized values")
{
    const ParamSpec linear { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear };
    CHECK(linear.toValue(0.25f) == doctest::Approx(25.0f));
    CHECK(linear.toNormalized(75.0f) == doctest::Approx(0.75f));
    CHECK(linear.toValue(2.0f) == doctest::Approx(100.0f));

    const ParamSpec log { "Time", "ms", 10.0f, 1000.0f, 100.0f, ParamScale::Log };
    CHECK(log.toValue(0.5f) == doctest::Approx(100.0f));
    CHECK(log.toNormalized(100.0f) == doctest::Approx(0.5f));

    const ParamSpec choice { "Type", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice };
    CHECK(choice.toValue(0.6f) == 1.0f);
    CHECK(choice.toValue(0.9f) == 2.0f);

    const ParamSpec toggle { "On", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle };
    CHECK(toggle.toValue(0.4f) == 0.0f);
    CHECK(toggle.toValue(0.6f) == 1.0f);
}

TEST_CASE("tone stack is exactly flat at 0 dB")
{
    ToneStack tone;
    tone.prepare(kRate);
    std::vector<float> x = sine(440.0, 4096);
    std::vector<float> y = x;
    float* io[2] = { y.data(), y.data() };
    tone.process(io, 1, 4096);
    CHECK(y == x);
}

TEST_CASE("tone stack bands boost and cut their ranges")
{
    CHECK(toneStackGainDb(50.0, 12.0f, 0.0f, 0.0f) == doctest::Approx(12.0f).epsilon(0.1));
    CHECK(toneStackGainDb(50.0, -12.0f, 0.0f, 0.0f) == doctest::Approx(-12.0f).epsilon(0.1));
    CHECK(toneStackGainDb(800.0, 0.0f, 6.0f, 0.0f) == doctest::Approx(6.0f).epsilon(0.05));
    CHECK(toneStackGainDb(12000.0, 0.0f, 0.0f, 12.0f) == doctest::Approx(12.0f).epsilon(0.1));
    // a bass boost leaves the treble mostly alone
    CHECK(std::fabs(toneStackGainDb(8000.0, 12.0f, 0.0f, 0.0f)) < 0.5f);
}

TEST_CASE("limiter never lets the output exceed its ceiling")
{
    Limiter limiter;
    limiter.prepare(kRate);
    std::vector<float> x = sine(100.0, 9600, 4.0f);
    float* io[2] = { x.data(), x.data() };
    const float minGain = limiter.process(io, 1, 9600);
    CHECK(peak(x, 0) <= Limiter::kCeiling + 1e-6f);
    CHECK(minGain < 0.25f);

    std::vector<float> quiet = sine(100.0, 4800, 0.5f);
    const std::vector<float> original = quiet;
    float* qio[2] = { quiet.data(), quiet.data() };
    limiter.reset();
    CHECK(limiter.process(qio, 1, 4800) == 1.0f);
    CHECK(quiet == original);
}

TEST_CASE("meter tracks peaks and holds the clip flag")
{
    Meter meter;
    meter.prepare(kRate);
    std::vector<float> x(480, 0.0f);
    x[10] = 1.2f;
    meter.process(x.data(), 480);
    CHECK(meter.clipping());
    CHECK(meter.level() > 1.0f);

    std::vector<float> silence(48000, 0.0f);
    meter.process(silence.data(), 48000);
    CHECK_FALSE(meter.clipping());
    CHECK(meter.level() < 0.1f); // fallen by more than 20 dB after a second
}
