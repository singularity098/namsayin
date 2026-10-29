// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/RingModBlock.hpp"

#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kWaveNames[] = { "Sine", "Square" };

class RingModBlock : public Block {
public:
    RingModBlock()
    {
        fFrequency.snap(300.0f);
        fMix.snap(1.0f);
        fDepth.snap(0.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fFrequency.prepare(sampleRate, 0.03);
        fMix.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.05);
        fLfo.prepare(sampleRate);
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kRingParamFrequency: fFrequency.setTarget(value); break;
        case kRingParamMix:       fMix.setTarget(value / 100.0f); break;
        case kRingParamRate:      fLfo.setRate(value); break;
        case kRingParamDepth:     fDepth.setTarget(value / 100.0f); break;
        case kRingParamWave:      fSquare = value >= 0.5f; break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fFrequency.snap(fFrequency.target());
            fMix.snap(fMix.target());
            fDepth.snap(fDepth.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double lfo = Lfo::shape(LfoShape::Sine, fLfo.advance());
            const double hz = fFrequency.next() * std::exp2(fDepth.next() * lfo);
            fPhase += hz / fSampleRate;
            fPhase -= std::floor(fPhase);
            const float carrier = Lfo::shape(fSquare ? LfoShape::Square : LfoShape::Sine, fPhase);
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                io[ch][i] = x + (x * carrier - x) * mix;
            }
        }
    }

    void reset() override
    {
        fPhase = 0.0;
        fLfo.reset();
        fSnap = true;
    }

private:
    double fSampleRate = 48000.0;
    double fPhase = 0.0;
    bool fSquare = false;
    Lfo fLfo;
    Smoother fFrequency, fMix, fDepth;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kRingModBlockDescriptor = {
    "ringmod",
    "Ring Mod",
    Category::Modulation,
    kRingParamCount,
    {
        ParamSpec { "Frequency", "Hz", 20.0f, 3000.0f, 300.0f, ParamScale::Log },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Rate", "Hz", 0.05f, 10.0f, 1.0f, ParamScale::Log },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Wave", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kWaveNames },
    },
    [] { return std::unique_ptr<Block>(new RingModBlock()); },
};

} // namespace xythos
