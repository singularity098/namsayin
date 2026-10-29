// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/WarbleBlock.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr double kWowMs = 4.0;       // delay swing at 100% (about 25 cents at 0.5 Hz)
constexpr double kFlutterMs = 0.15;  // at 100% (about 15 cents)
constexpr double kFlutterHz = 9.0;
constexpr double kBaseMs = 1.0;

class WarbleBlock : public Block {
public:
    WarbleBlock()
    {
        fDepth.snap(0.3f);
        fFlutter.snap(0.15f);
        fMix.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(std::ceil((2.0 * kWowMs + 2.0 * kFlutterMs + kBaseMs + 2.0) / 1000.0 * sampleRate)));
        fWow.prepare(sampleRate);
        fFlutterLfo.prepare(sampleRate);
        fFlutterLfo.setRate(kFlutterHz);
        updateRate();
        fDepth.prepare(sampleRate, 0.05);
        fFlutter.prepare(sampleRate, 0.05);
        fMix.prepare(sampleRate);
        updateTone();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kWarbleParamDepth:   fDepth.setTarget(value / 100.0f); break;
        case kWarbleParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kWarbleParamFlutter: fFlutter.setTarget(value / 100.0f); break;
        case kWarbleParamRandom:  fRandomAmount = value / 100.0f; break;
        case kWarbleParamTone:
            fTone = value;
            updateTone();
            break;
        case kWarbleParamDrive: fDrive = value / 100.0f; break;
        case kWarbleParamMix:   fMix.setTarget(value / 100.0f); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fFlutter.snap(fFlutter.target());
            fMix.snap(fMix.target());
        }
        const float msToSamples = static_cast<float>(fSampleRate / 1000.0);
        const float driveGain = 1.0f + 5.0f * fDrive;
        for (uint32_t i = 0; i < frames; ++i)
        {
            // wow: a sine, or as Random goes up a wandering curve; both run, so turning Random never jumps
            const float sine = Lfo::shape(LfoShape::Sine, fWow.advance());
            const float wander = fRandom.next();
            const float wow = sine + (wander - sine) * fRandomAmount;
            const float flutter = Lfo::shape(LfoShape::Sine, fFlutterLfo.advance());
            const float delay = msToSamples * static_cast<float>(kBaseMs + kWowMs * fDepth.next() * (1.0f + wow)
                                                                 + kFlutterMs * fFlutter.next() * (1.0f + flutter));
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float y = fLines[ch].readFractional(std::max(1.0f, delay));
                if (fDrive > 0.0f)
                    y = shaper::soft(y * driveGain) / driveGain * (1.0f + 0.5f * fDrive);
                if (fToneOn)
                    y = fToneFilter[ch].process(y);
                io[ch][i] = x + (y - x) * mix;
            }
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fLines[ch].reset();
            fToneFilter[ch].reset();
        }
        fWow.reset();
        fFlutterLfo.reset();
        fRandom.prepare(fSampleRate, fRateHz, 0x9e3779b9u);
        fSnap = true;
    }

private:
    void updateRate()
    {
        fWow.setRate(fRateHz);
        fRandom.setRate(fSampleRate, fRateHz);
    }

    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        for (Biquad& filter : fToneFilter)
            filter.setLowPass(fSampleRate, std::min<double>(fTone, 0.45 * fSampleRate));
    }

    double fSampleRate = 48000.0;
    float fRateHz = 0.6f;
    float fRandomAmount = 0.4f;
    float fTone = 5000.0f;
    bool fToneOn = true;
    float fDrive = 0.2f;
    DelayLine fLines[2];
    Biquad fToneFilter[2];
    Lfo fWow, fFlutterLfo;
    RandomLfo fRandom;
    Smoother fDepth, fFlutter, fMix;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kWarbleBlockDescriptor = {
    "warble",
    "Warble",
    Category::Modulation,
    kWarbleParamCount,
    {
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Rate", "Hz", 0.1f, 5.0f, 0.6f, ParamScale::Log },
        ParamSpec { "Flutter", "%", 0.0f, 100.0f, 15.0f, ParamScale::Linear },
        ParamSpec { "Random", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 1000.0f, 20000.0f, 5000.0f, ParamScale::Log },
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 20.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new WarbleBlock()); },
};

} // namespace xythos
