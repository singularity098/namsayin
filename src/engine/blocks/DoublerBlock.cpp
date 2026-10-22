// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/DoublerBlock.hpp"

#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kVoiceNames[] = { "Single", "Double" };

constexpr double kMaxDelayMs = 80.0;
constexpr float kMaxDriftMs = 3.0f;  // timing wander each way at 100%
constexpr double kDriftHz = 0.7;     // how often the wander changes direction
constexpr float kSecondVoiceRatio = 1.37f; // the second take's delay, relative to the first

class DoublerBlock : public Block {
public:
    DoublerBlock()
    {
        fDelay.snap(18.0f);
        fDrift.snap(0.4f);
        fWidth.snap(1.0f);
        fMix.snap(0.5f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLine.prepare(static_cast<size_t>(std::ceil(kMaxDelayMs / 1000.0 * sampleRate)) + 4);
        for (Smoother* s : { &fDelay, &fDrift, &fWidth, &fMix })
            s->prepare(sampleRate, 0.05);
        updateTone();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kDoublerParamVoices:
            fVoices = value >= 0.5f ? 2 : 1;
            break;
        case kDoublerParamDelay:
            fDelay.setTarget(value);
            break;
        case kDoublerParamDrift:
            fDrift.setTarget(value / 100.0f);
            break;
        case kDoublerParamTone:
            fTone = value;
            updateTone();
            break;
        case kDoublerParamWidth:
            fWidth.setTarget(value / 100.0f);
            break;
        case kDoublerParamMix:
            fMix.setTarget(value / 100.0f);
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            for (Smoother* s : { &fDelay, &fDrift, &fWidth, &fMix })
                s->snap(s->target());
        }

        const float msToSamples = static_cast<float>(fSampleRate / 1000.0);
        const float maxDelay = static_cast<float>(fLine.capacity() - 4);
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float delayMs = fDelay.next();
            const float drift = fDrift.next() * kMaxDriftMs;
            const float width = fWidth.next();
            const float mix = fMix.next();

            // the doubles come from the mono sum: they're new takes of the part, not copies of each side
            const float x = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            fLine.write(x);

            float takes[2];
            for (int v = 0; v < fVoices; ++v)
            {
                const float ms = delayMs * (v == 0 ? 1.0f : kSecondVoiceRatio) + drift * fWander[v].next();
                float take = fLine.readFractional(std::clamp(ms * msToSamples, 1.0f, maxDelay));
                if (fToneOn)
                    take = fToneFilter[v].lowPass(take);
                takes[v] = fHighPass[v].highPass(take);
            }

            const float wetGain = wetGainForMix(mix);
            const float dryGain = dryGainForMix(mix);
            if (numChannels == 1)
            {
                const float wet = fVoices == 2 ? 0.7071f * (takes[0] + takes[1]) : takes[0];
                io[0][i] = io[0][i] * dryGain + wet * wetGain;
                continue;
            }

            // pan the takes out: one take goes to one side; two go one each way
            float left, right;
            const float side = 0.5f + 0.5f * width; // the near side's share
            if (fVoices == 2)
            {
                left = takes[0] * side + takes[1] * (1.0f - side);
                right = takes[1] * side + takes[0] * (1.0f - side);
            }
            else
            {
                left = takes[0] * (1.0f - side);
                right = takes[0] * side;
            }
            io[0][i] = io[0][i] * dryGain + left * wetGain * 1.4142f;
            io[1][i] = io[1][i] * dryGain + right * wetGain * 1.4142f;
        }
    }

    void reset() override
    {
        fLine.reset();
        for (int v = 0; v < 2; ++v)
        {
            fToneFilter[v].reset();
            fHighPass[v].reset();
            fWander[v].prepare(fSampleRate, kDriftHz * (v == 0 ? 1.0 : 1.31), 0x9e3779b9u + 7919u * v);
        }
        fSnap = true;
    }

private:
    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        for (int v = 0; v < 2; ++v)
        {
            fToneFilter[v].setLowPass(fSampleRate, fTone);
            fHighPass[v].setLowPass(fSampleRate, 80.0); // used as a high-pass: keeps the doubles out of the lows
        }
    }

    double fSampleRate = 48000.0;
    int fVoices = 1;
    float fTone = 9000.0f;
    bool fToneOn = true;
    DelayLine fLine;
    OnePole fToneFilter[2], fHighPass[2];
    RandomLfo fWander[2];
    Smoother fDelay, fDrift, fWidth, fMix;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kDoublerBlockDescriptor = {
    "doubler",
    "Doubler",
    Category::Modulation,
    kDoublerParamCount,
    {
        ParamSpec { "Voices", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kVoiceNames },
        ParamSpec { "Delay", "ms", 5.0f, 50.0f, 18.0f, ParamScale::Log },
        ParamSpec { "Drift", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 1000.0f, 20000.0f, 9000.0f, ParamScale::Log },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new DoublerBlock()); },
};

} // namespace namsayin
