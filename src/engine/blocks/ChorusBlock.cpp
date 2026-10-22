// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/ChorusBlock.hpp"

#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kTypeNames[] = { "Classic", "Dimension", "Tri", "Vibrato" };

struct Voicing {
    int voices;
    float centerMs;   // average delay
    float depthMs;    // modulation at 100% depth (each way)
    float spacing;    // LFO phase between voices, in cycles
    bool wetOnly;
};

constexpr Voicing kVoicings[] = {
    { 1, 7.0f, 3.0f, 0.0f, false },        // Classic
    { 2, 5.0f, 1.2f, 0.25f, false },       // Dimension
    { 3, 9.0f, 3.0f, 1.0f / 3.0f, false }, // Tri
    { 1, 5.0f, 2.5f, 0.0f, true },         // Vibrato
};

constexpr double kMaxDelayMs = 20.0;

class ChorusBlock : public Block {
public:
    ChorusBlock()
    {
        fDepth.snap(0.5f);
        fMix.snap(0.5f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(std::ceil(kMaxDelayMs / 1000.0 * sampleRate)) + 4);
        fLfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.05);
        fMix.prepare(sampleRate);
        updateTone();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kChorusParamRate:
            fLfo.setRate(value);
            break;
        case kChorusParamDepth:
            fDepth.setTarget(value / 100.0f);
            break;
        case kChorusParamMix:
            fMix.setTarget(value / 100.0f);
            break;
        case kChorusParamType:
            fType = static_cast<ChorusType>(std::clamp(static_cast<int>(value), 0, 3));
            break;
        case kChorusParamTone:
            fTone = value;
            updateTone();
            break;
        case kChorusParamWidth:
            fWidth = value / 100.0f;
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fMix.snap(fMix.target());
        }

        const Voicing& v = kVoicings[static_cast<int>(fType)];
        const float msToSamples = static_cast<float>(fSampleRate / 1000.0);
        const float voiceGain = 1.0f / std::sqrt(static_cast<float>(v.voices));
        // the right side's LFO offset: up to half a cycle (a quarter for multi-voice types, which are already wide)
        const double sideOffset = fWidth * (v.voices > 1 ? 0.25 : 0.5);

        for (uint32_t i = 0; i < frames; ++i)
        {
            const double phase = fLfo.advance();
            const float depth = fDepth.next();
            const float mix = v.wetOnly ? 1.0f : fMix.next();
            const float dryGain = v.wetOnly ? 0.0f : dryGainForMix(mix);
            const float wetGain = v.wetOnly ? 1.0f : wetGainForMix(mix);

            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float wet = 0.0f;
                for (int k = 0; k < v.voices; ++k)
                {
                    const double p = phase + k * v.spacing + (ch == 1 ? sideOffset : 0.0);
                    const float delayMs = v.centerMs + depth * v.depthMs * Lfo::shape(LfoShape::Triangle, p);
                    wet += fLines[ch].readFractional(std::max(1.0f, delayMs * msToSamples));
                }
                wet *= voiceGain;
                if (fToneOn)
                    wet = fToneFilter[ch].lowPass(wet);
                io[ch][i] = x * dryGain + wet * wetGain;
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
        fLfo.reset();
        fSnap = true;
    }

private:
    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        for (OnePole& filter : fToneFilter)
            filter.setLowPass(fSampleRate, fTone);
    }

    double fSampleRate = 48000.0;
    ChorusType fType = ChorusType::Classic;
    float fTone = 8000.0f;
    bool fToneOn = true;
    float fWidth = 1.0f;
    DelayLine fLines[2];
    OnePole fToneFilter[2];
    Lfo fLfo;
    Smoother fDepth, fMix;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kChorusBlockDescriptor = {
    "chorus",
    "Chorus",
    Category::Modulation,
    kChorusParamCount,
    {
        ParamSpec { "Rate", "Hz", 0.05f, 10.0f, 0.8f, ParamScale::Log },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Tone", "Hz", 1000.0f, 20000.0f, 8000.0f, ParamScale::Log },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ChorusBlock()); },
};

} // namespace namsayin
