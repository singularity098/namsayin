// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/BitcrusherBlock.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

class BitcrusherBlock : public Block {
public:
    BitcrusherBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        updateTone();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kCrushParamBits:
            fSteps = std::exp2(std::clamp(value, 1.0f, 16.0f) - 1.0f);
            break;
        case kCrushParamRate:
            fRate = value;
            break;
        case kCrushParamTone:
            fTone = value;
            updateTone();
            break;
        case kCrushParamMix:
            fMix.setTarget(value / 100.0f);
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
        }
        const double step = std::min(1.0, fRate / fSampleRate);
        for (uint32_t i = 0; i < frames; ++i)
        {
            // sample-and-hold at the reduced rate
            fHoldPhase += step;
            const bool take = fHoldPhase >= 1.0;
            if (take)
                fHoldPhase -= 1.0;
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                if (take)
                    fHeld[ch] = std::round(std::clamp(x, -1.0f, 1.0f) * fSteps) / fSteps;
                float y = fHeld[ch];
                if (fToneOn)
                    y = fToneFilter[ch].process(y);
                io[ch][i] = x + (y - x) * mix;
            }
        }
    }

    void reset() override
    {
        fHoldPhase = 1.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            fHeld[ch] = 0.0f;
            fToneFilter[ch].reset();
        }
        fSnap = true;
    }

private:
    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        for (Biquad& filter : fToneFilter)
            filter.setLowPass(fSampleRate, std::min<double>(fTone, 0.45 * fSampleRate));
    }

    double fSampleRate = 48000.0;
    float fSteps = 128.0f;
    float fRate = 12000.0f;
    float fTone = 8000.0f;
    bool fToneOn = true;
    double fHoldPhase = 1.0;
    float fHeld[2] {};
    Biquad fToneFilter[2];
    Smoother fMix;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kBitcrusherBlockDescriptor = {
    "bitcrusher",
    "Bitcrusher",
    Category::Distortion,
    kCrushParamCount,
    {
        ParamSpec { "Bits", "", 1.0f, 16.0f, 8.0f, ParamScale::Linear },
        ParamSpec { "Rate", "Hz", 500.0f, 48000.0f, 12000.0f, ParamScale::Log },
        ParamSpec { "Tone", "Hz", 500.0f, 20000.0f, 8000.0f, ParamScale::Log },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new BitcrusherBlock()); },
};

} // namespace xythos
