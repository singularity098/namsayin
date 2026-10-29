// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace xythos {

// Circular delay line with fractional reads. The buffer is sized in prepare() (non-RT); the rest is RT-safe.
class DelayLine {
public:
    // Non-RT. Holds at least `maxDelay` samples of history.
    void prepare(size_t maxDelay)
    {
        size_t size = 1;
        while (size < maxDelay + 4)
            size <<= 1;
        fBuffer.assign(size, 0.0f);
        fMask = size - 1;
        fWrite = 0;
    }

    void reset() { std::fill(fBuffer.begin(), fBuffer.end(), 0.0f); }

    void write(float x)
    {
        fBuffer[fWrite] = x;
        fWrite = (fWrite + 1) & fMask;
    }

    // The sample written `delay` samples before the last write; delay >= 1
    float read(size_t delay) const { return fBuffer[(fWrite - delay) & fMask]; }

    // Cubic (Hermite) interpolated read, for modulated and smoothly changing delays; delay >= 1
    float readFractional(float delay) const
    {
        const float whole = std::floor(delay);
        const float t = delay - whole;
        const size_t i = static_cast<size_t>(whole);
        const float y0 = i > 1 ? read(i - 1) : read(i);
        const float y1 = read(i);
        const float y2 = read(i + 1);
        const float y3 = read(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    size_t capacity() const { return fBuffer.size() - 4; }

private:
    std::vector<float> fBuffer { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    size_t fMask = 0;
    size_t fWrite = 0;
};

// Sine LFO by rotating a phasor: no trig per sample
class SineLfo {
public:
    void prepare(double sampleRate, double hz, double phase = 0.0)
    {
        const double w = 2.0 * M_PI * hz / sampleRate;
        fCos = std::cos(w);
        fSin = std::sin(w);
        fX = std::cos(phase);
        fY = std::sin(phase);
    }

    // Changes the rate and keeps the phase. RT-safe.
    void setRate(double sampleRate, double hz)
    {
        const double w = 2.0 * M_PI * hz / sampleRate;
        fCos = std::cos(w);
        fSin = std::sin(w);
    }

    float next()
    {
        const double x = fX * fCos - fY * fSin;
        const double y = fX * fSin + fY * fCos;
        // renormalize so rounding doesn't grow or shrink the amplitude
        const double norm = 1.5 - 0.5 * (x * x + y * y);
        fX = x * norm;
        fY = y * norm;
        return static_cast<float>(fY);
    }

    // The current value shifted by a phase offset, given as its cosine and sine (no trig per sample)
    float shifted(float cosOffset, float sinOffset) const
    {
        return static_cast<float>(fY * cosOffset + fX * sinOffset);
    }

private:
    double fCos = 1.0, fSin = 0.0, fX = 1.0, fY = 0.0;
};

// Equal-loudness-ish dry/wet balance used by the time-based blocks: dry stays at full level up to 50% mix,
// wet reaches full level at 50%
inline float dryGainForMix(float mix)
{
    return std::fmin(1.0f, 2.0f * (1.0f - mix));
}

inline float wetGainForMix(float mix)
{
    return std::fmin(1.0f, 2.0f * mix);
}

// Fixed delay of up to two channels, used to time-align the branches of a split
class AlignmentDelay {
public:
    // Non-RT
    void prepare(uint32_t delay)
    {
        fDelay = delay;
        for (std::vector<float>& buffer : fBuffer)
            buffer.assign(delay, 0.0f);
        fPos = 0;
    }

    uint32_t delay() const { return fDelay; }

    // RT. In place.
    void process(float* const* io, uint32_t numChannels, uint32_t frames)
    {
        if (fDelay == 0)
            return;
        uint32_t pos = fPos;
        for (uint32_t ch = 0; ch < numChannels; ++ch)
        {
            float* buffer = fBuffer[ch].data();
            pos = fPos;
            for (uint32_t i = 0; i < frames; ++i)
            {
                const float out = buffer[pos];
                buffer[pos] = io[ch][i];
                io[ch][i] = out;
                if (++pos == fDelay)
                    pos = 0;
            }
        }
        fPos = pos;
    }

private:
    std::vector<float> fBuffer[2];
    uint32_t fDelay = 0;
    uint32_t fPos = 0;
};

} // namespace xythos
