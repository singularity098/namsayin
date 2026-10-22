// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/SpringReverbBlock.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

constexpr int kStages = 60;     // allpasses per spring
constexpr int kMaxStretch = 64; // the longest stretch, for low chirp frequencies at high sample rates
constexpr double kMaxDelayMs = 80.0;

// A spring: dispersion chain, then the transit delay, then damping, fed back
class Spring {
public:
    void prepare(double sampleRate)
    {
        fLine.prepare(static_cast<size_t>(std::ceil(kMaxDelayMs / 1000.0 * sampleRate)));
        reset();
    }

    void reset()
    {
        for (auto& stage : fX)
            stage.fill(0.0f);
        for (auto& stage : fY)
            stage.fill(0.0f);
        fPos = 0;
        fLine.reset();
        fDamping.reset();
        fLast = 0.0f;
    }

    void set(int stretch, float coefficient, float delaySamples, float feedback, double sampleRate, double toneHz)
    {
        fStretch = std::clamp(stretch, 1, kMaxStretch);
        fA = coefficient;
        fDelay = delaySamples;
        fFeedback = feedback;
        fDamping.setLowPass(sampleRate, toneHz);
    }

    float process(float x)
    {
        float v = x + fFeedback * fLast;
        // stretched first-order allpasses: y[n] = a x[n] + x[n-K] - a y[n-K]
        const int read = (fPos + kMaxStretch - fStretch) % kMaxStretch;
        for (int s = 0; s < kStages; ++s)
        {
            const float y = fA * v + fX[s][read] - fA * fY[s][read];
            fX[s][fPos] = v;
            fY[s][fPos] = y;
            v = y;
        }
        fPos = (fPos + 1) % kMaxStretch;
        fLine.write(v);
        fLast = fDamping.lowPass(fLine.readFractional(fDelay));
        return fLast;
    }

private:
    std::array<std::array<float, kMaxStretch>, kStages> fX {}, fY {};
    int fPos = 0;
    int fStretch = 5;
    float fA = 0.6f;
    float fDelay = 2000.0f;
    float fFeedback = 0.5f;
    float fLast = 0.0f;
    DelayLine fLine;
    OnePole fDamping;
};

class SpringReverbBlock : public Block {
public:
    SpringReverbBlock()
    {
        fMix.snap(0.3f);
        fDwell.snap(0.5f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (Spring& spring : fSprings)
            spring.prepare(sampleRate);
        fMix.prepare(sampleRate);
        fDwell.prepare(sampleRate);
        for (Biquad& filter : fInputHighPass)
            filter.setHighPass(sampleRate, 120.0);
        applyAll();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kSpringParamDecay:   fDecay = value; break;
        case kSpringParamTension: fTension = value / 100.0f; break;
        case kSpringParamDrip:    fDrip = value / 100.0f; break;
        case kSpringParamTone:    fTone = value; break;
        case kSpringParamDwell:   fDwell.setTarget(value / 100.0f); break;
        case kSpringParamMix:     fMix.setTarget(value / 100.0f); break;
        case kSpringParamWidth:   fWidth = value / 100.0f; break;
        }
        applyAll();
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
            fDwell.snap(fDwell.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float inL = io[0][i];
            const float inR = numChannels > 1 ? io[1][i] : inL;
            const float mix = fMix.next();
            const float dwell = fDwell.next();

            // the reverb driver: mono into both springs, overdriven by Dwell
            const float driveGain = 1.0f + 8.0f * dwell * dwell;
            const float drive = shaper::soft(fInputHighPass[0].process(0.5f * (inL + inR)) * driveGain) / std::sqrt(driveGain);
            const float a = fOutputLowPass[0].process(fSprings[0].process(drive));
            const float b = fOutputLowPass[1].process(fSprings[1].process(drive));

            const float dryGain = dryGainForMix(mix), wetGain = wetGainForMix(mix) * 1.5f;
            if (numChannels == 1)
            {
                io[0][i] = inL * dryGain + 0.5f * (a + b) * wetGain;
            }
            else
            {
                // Width 0: both springs in the middle; 100: one per side
                const float side = 0.5f + 0.5f * fWidth;
                io[0][i] = inL * dryGain + (a * side + b * (1.0f - side)) * wetGain;
                io[1][i] = inR * dryGain + (b * side + a * (1.0f - side)) * wetGain;
            }
        }
    }

    void reset() override
    {
        for (Spring& spring : fSprings)
            spring.reset();
        for (int k = 0; k < 2; ++k)
        {
            fInputHighPass[k].reset();
            fOutputLowPass[k].reset();
        }
        fSnap = true;
    }

private:
    void applyAll()
    {
        // chirp frequency (the dispersion's upper limit) and transit time from the tension; the two springs differ a
        // little, as in a real tank
        const double chirpHz = 2500.0 + 3500.0 * fTension;
        const double transitMs = 60.0 - 30.0 * fTension;
        const float a = 0.35f + 0.4f * fDrip;
        for (int k = 0; k < 2; ++k)
        {
            const double spread = k == 0 ? 1.0 : 1.13;
            const int stretch = static_cast<int>(std::lround(fSampleRate / (2.0 * chirpHz * spread)));
            const double groupDelay = kStages * std::max(1, stretch) * (1.0 - a) / (1.0 + a);
            const double delay = transitMs * spread / 1000.0 * fSampleRate;
            const double loop = delay + groupDelay;
            const float feedback = static_cast<float>(std::pow(10.0, -3.0 * loop / (fDecay * fSampleRate)));
            fSprings[k].set(stretch, a, static_cast<float>(delay), -feedback, fSampleRate, fTone);
            // the stretched allpasses mirror their response above the chirp frequency; keep only the band below
            fOutputLowPass[k].setLowPass(fSampleRate, std::min(chirpHz * spread, 0.45 * fSampleRate), 0.6);
        }
    }

    double fSampleRate = 48000.0;
    Spring fSprings[2];
    Biquad fInputHighPass[2], fOutputLowPass[2];
    Smoother fMix, fDwell;
    float fDecay = 2.5f;
    float fTension = 0.5f;
    float fDrip = 0.6f;
    float fTone = 5000.0f;
    float fWidth = 0.6f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kSpringReverbBlockDescriptor = {
    "reverb.spring",
    "Spring Reverb",
    Category::Reverb,
    kSpringParamCount,
    {
        ParamSpec { "Decay", "s", 0.5f, 6.0f, 2.5f, ParamScale::Log },
        ParamSpec { "Tension", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Drip", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 1000.0f, 10000.0f, 5000.0f, ParamScale::Log },
        ParamSpec { "Dwell", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SpringReverbBlock()); },
};

} // namespace namsayin
