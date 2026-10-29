// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/EqBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

// Band filters are recomputed when a gain or frequency changes. Changes are smoothed by moving the design
// parameters a step at a time, every kUpdateInterval samples, so sweeping a knob doesn't click.
constexpr uint32_t kUpdateInterval = 32;

class GraphicEqBlock : public Block {
public:
    GraphicEqBlock()
    {
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLevel.prepare(sampleRate);
        for (int b = 0; b < kGraphicEqBands; ++b)
            fGain[b].prepare(sampleRate / kUpdateInterval, 0.03);
        fSnap = true;
        updateFilters();
        reset();
    }

    void setParam(int index, float value) override
    {
        if (index >= 0 && index < kGraphicEqBands)
            fGain[index].setTarget(value);
        else if (index == kGraphicEqParamLevel)
            fLevel.setTarget(dbToGain(value));
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            for (Smoother& gain : fGain)
                gain.snap(gain.target());
            fLevel.snap(fLevel.target());
            updateFilters();
        }

        for (uint32_t i = 0; i < frames; ++i)
        {
            if (++fCounter >= kUpdateInterval)
            {
                fCounter = 0;
                bool moving = false;
                for (Smoother& gain : fGain)
                    moving |= gain.advance();
                if (moving)
                    updateFilters();
            }
            const float level = fLevel.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                float x = io[ch][i];
                for (int b = 0; b < kGraphicEqBands; ++b)
                    x = fFilters[ch][b].process(x);
                io[ch][i] = x * level;
            }
        }
    }

    void reset() override
    {
        for (auto& channel : fFilters)
            for (Biquad& filter : channel)
                filter.reset();
    }

private:
    void updateFilters()
    {
        // Q of about 1.4: one-octave bands that overlap like a pedal's
        for (int b = 0; b < kGraphicEqBands; ++b)
            for (auto& channel : fFilters)
                channel[b].setPeak(fSampleRate, kGraphicEqFreqs[b], fGain[b].value(), 1.4);
    }

    double fSampleRate = 48000.0;
    Biquad fFilters[2][kGraphicEqBands];
    Smoother fGain[kGraphicEqBands];
    Smoother fLevel;
    uint32_t fCounter = 0;
    bool fSnap = true;
};

class ParametricEqBlock : public Block {
public:
    ParametricEqBlock()
    {
        fLowCut.snap(20.0f);
        fMidFreq.snap(800.0f);
        fQ.snap(1.0f);
        fHighCut.snap(20000.0f);
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        const double controlRate = sampleRate / kUpdateInterval;
        for (Smoother* s : { &fLowCut, &fLow, &fMidFreq, &fMid, &fQ, &fHigh, &fHighCut })
            s->prepare(controlRate, 0.03);
        fLevel.prepare(sampleRate);
        fSnap = true;
        updateFilters();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kParEqParamLowCut:  fLowCut.setTarget(value); break;
        case kParEqParamLow:     fLow.setTarget(value); break;
        case kParEqParamMidFreq: fMidFreq.setTarget(value); break;
        case kParEqParamMid:     fMid.setTarget(value); break;
        case kParEqParamQ:       fQ.setTarget(value); break;
        case kParEqParamHigh:    fHigh.setTarget(value); break;
        case kParEqParamHighCut: fHighCut.setTarget(value); break;
        case kParEqParamLevel:   fLevel.setTarget(dbToGain(value)); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        Smoother* controls[] = { &fLowCut, &fLow, &fMidFreq, &fMid, &fQ, &fHigh, &fHighCut };
        if (fSnap)
        {
            fSnap = false;
            for (Smoother* s : controls)
                s->snap(s->target());
            fLevel.snap(fLevel.target());
            updateFilters();
        }

        for (uint32_t i = 0; i < frames; ++i)
        {
            if (++fCounter >= kUpdateInterval)
            {
                fCounter = 0;
                bool moving = false;
                for (Smoother* s : controls)
                    moving |= s->advance();
                if (moving)
                    updateFilters();
            }
            const float level = fLevel.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                float x = io[ch][i];
                if (fLowCutOn)
                    x = fFilters[ch][0].process(x);
                for (int f = 1; f < 4; ++f)
                    x = fFilters[ch][f].process(x);
                if (fHighCutOn)
                    x = fFilters[ch][4].process(x);
                io[ch][i] = x * level;
            }
        }
    }

    void reset() override
    {
        for (auto& channel : fFilters)
            for (Biquad& filter : channel)
                filter.reset();
    }

private:
    void updateFilters()
    {
        const auto& spec = kParametricEqBlockDescriptor.params;
        fLowCutOn = fLowCut.value() > spec[kParEqParamLowCut].min * 1.01f;
        fHighCutOn = fHighCut.value() < spec[kParEqParamHighCut].max * 0.99f;
        const double nyquistSafe = 0.45 * fSampleRate;
        for (auto& channel : fFilters)
        {
            channel[0].setHighPass(fSampleRate, fLowCut.value());
            channel[1].setLowShelf(fSampleRate, 100.0, fLow.value());
            channel[2].setPeak(fSampleRate, std::min<double>(fMidFreq.value(), nyquistSafe), fMid.value(), fQ.value());
            channel[3].setHighShelf(fSampleRate, std::min(4000.0, nyquistSafe), fHigh.value());
            channel[4].setLowPass(fSampleRate, std::min<double>(fHighCut.value(), nyquistSafe));
        }
    }

    double fSampleRate = 48000.0;
    Biquad fFilters[2][5];
    Smoother fLowCut, fLow, fMidFreq, fMid, fQ, fHigh, fHighCut, fLevel;
    bool fLowCutOn = false, fHighCutOn = false;
    uint32_t fCounter = 0;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kGraphicEqBlockDescriptor = {
    "eq.graphic",
    "Graphic EQ",
    Category::Eq,
    kGraphicEqBands + 1,
    {
        ParamSpec { "100", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "200", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "400", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "800", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "1.6k", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "3.2k", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "6.4k", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new GraphicEqBlock()); },
};

const BlockDescriptor kParametricEqBlockDescriptor = {
    "eq.parametric",
    "Parametric EQ",
    Category::Eq,
    kParEqParamCount,
    {
        ParamSpec { "Low Cut", "Hz", 20.0f, 500.0f, 20.0f, ParamScale::Log },
        ParamSpec { "Low", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mid Freq", "Hz", 150.0f, 6000.0f, 800.0f, ParamScale::Log },
        ParamSpec { "Mid", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Q", "", 0.3f, 8.0f, 1.0f, ParamScale::Log },
        ParamSpec { "High", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "High Cut", "Hz", 2000.0f, 20000.0f, 20000.0f, ParamScale::Log },
        ParamSpec { "Level", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ParametricEqBlock()); },
};

} // namespace xythos
