// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

// An 8-line feedback delay network reverb tank: input diffusers, then eight delay lines mixed through a
// Householder matrix (lossless), each with a decay gain set from the RT60 and a one-pole damping filter. Two lines are
// slowly modulated against metallic ringing. Frozen, the loop is lossless (no decay, no damping) and the input is
// shut, so the sound holds indefinitely. Used by the shimmer and freeze blocks; stereo in and out.
class Fdn {
public:
    static constexpr int kLines = 8;

    // Non-RT. maxSize scales every length (1 = the base lengths).
    void prepare(double sampleRate, double maxSize)
    {
        fSampleRate = sampleRate;
        fMaxSize = maxSize;
        for (int i = 0; i < kLines; ++i)
            fLines[i].prepare(static_cast<size_t>(std::ceil((kLineMs[i] * maxSize + kModMs + 2.0) / 1000.0 * sampleRate)));
        for (int i = 0; i < kDiffusers; ++i)
            for (int ch = 0; ch < 2; ++ch)
                fDiffusers[ch][i].prepare(static_cast<size_t>(std::ceil(kDiffuserMs[i] * 1.1 / 1000.0 * sampleRate)));
        fMod[0].prepare(sampleRate);
        fMod[1].prepare(sampleRate);
        fMod[0].setRate(0.31);
        fMod[1].setRate(0.47);
        setSize(1.0);
        reset();
    }

    void setSize(double size)
    {
        fSize = std::clamp(size, 0.1, fMaxSize);
        for (int i = 0; i < kLines; ++i)
            fLength[i] = static_cast<float>(kLineMs[i] * fSize / 1000.0 * fSampleRate);
        updateGains();
    }

    void setDecay(double seconds)
    {
        fDecay = std::max(0.05, seconds);
        updateGains();
    }

    void setDamping(double hz)
    {
        for (OnePole& filter : fDamping)
            filter.setLowPass(fSampleRate, hz);
    }

    void setFrozen(bool frozen) { fFrozen = frozen; }
    bool frozen() const { return fFrozen; }

    void reset()
    {
        for (DelayLine& line : fLines)
            line.reset();
        for (auto& channel : fDiffusers)
            for (DelayLine& line : channel)
                line.reset();
        for (OnePole& filter : fDamping)
            filter.reset();
        fMod[0].reset();
        fMod[1].reset(0.5);
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        if (fFrozen)
            inL = inR = 0.0f;
        inL = diffuse(0, inL);
        inR = diffuse(1, inR);

        const float modSamples = static_cast<float>(kModMs / 1000.0 * fSampleRate);
        const float mod0 = Lfo::shape(LfoShape::Sine, fMod[0].advance()) * modSamples;
        const float mod1 = Lfo::shape(LfoShape::Sine, fMod[1].advance()) * modSamples;

        float d[kLines];
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            float delay = fLength[i];
            if (i == 2)
                delay += mod0;
            else if (i == 5)
                delay += mod1;
            float v = fLines[i].readFractional(std::max(2.0f, delay));
            // frozen: a hair under lossless, so rounding can never make it grow
            v = fFrozen ? v * kFrozenGain : fDamping[i].lowPass(v) * fGain[i];
            d[i] = v;
            sum += v;
        }

        outL = d[0] - d[2] + d[4] - d[6] + 0.5f * (d[1] - d[7]);
        outR = d[1] - d[3] + d[5] - d[7] + 0.5f * (d[2] - d[4]);
        outL *= 0.35f;
        outR *= 0.35f;

        // Householder: lossless mixing of every line into every other
        const float householder = sum * (2.0f / kLines);
        for (int i = 0; i < kLines; ++i)
        {
            const float input = (i % 2 == 0 ? inL : inR) * 0.5f;
            fLines[i].write(d[i] - householder + input);
        }
    }

private:
    static constexpr int kDiffusers = 4;
    static constexpr double kLineMs[kLines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 79.3 };
    static constexpr double kDiffuserMs[kDiffusers] = { 4.77, 3.59, 12.73, 9.31 };
    static constexpr double kModMs = 0.6;
    static constexpr float kDiffusion = 0.6f;
    static constexpr float kFrozenGain = 0.99998f;

    float diffuse(int ch, float x)
    {
        for (int i = 0; i < kDiffusers; ++i)
        {
            DelayLine& line = fDiffusers[ch][i];
            const size_t delay = static_cast<size_t>(kDiffuserMs[i] * (ch == 0 ? 1.0 : 1.07) / 1000.0 * fSampleRate);
            const float delayed = line.read(std::max<size_t>(1, delay));
            const float v = x + kDiffusion * delayed;
            line.write(v);
            x = delayed - kDiffusion * v;
        }
        return x;
    }

    void updateGains()
    {
        for (int i = 0; i < kLines; ++i)
            fGain[i] = static_cast<float>(std::pow(10.0, -3.0 * fLength[i] / (fDecay * fSampleRate)));
    }

    double fSampleRate = 48000.0;
    double fMaxSize = 1.0;
    double fSize = 1.0;
    double fDecay = 3.0;
    bool fFrozen = false;
    DelayLine fLines[kLines];
    DelayLine fDiffusers[2][kDiffusers];
    OnePole fDamping[kLines];
    Lfo fMod[2];
    float fLength[kLines] {};
    float fGain[kLines] {};
};

} // namespace xythos
