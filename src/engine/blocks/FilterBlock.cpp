// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/FilterBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace namsayin {

namespace {

constexpr const char* kTypeNames[] = { "Low-pass", "Band-pass", "High-pass", "Notch" };
constexpr const char* kShapeNames[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Steps" };
constexpr float kDepthOctaves = 3.0f;

class FilterBlock : public Block {
public:
    FilterBlock()
    {
        fDepth.snap(0.5f);
        fMix.snap(1.0f);
        fCutoff.snap(800.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.03);
        fMix.prepare(sampleRate);
        fCutoff.prepare(sampleRate, 0.03);
        // the steps glide for a millisecond, so they don't click
        fStepSmooth.prepare(sampleRate, 0.001);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kFilterParamType:      fType = std::clamp(static_cast<int>(value), 0, 3); break;
        case kFilterParamCutoff:    fCutoff.setTarget(value); break;
        case kFilterParamResonance: fQ = value; break;
        case kFilterParamShape:     fShape = std::clamp(static_cast<int>(value), 0, 5); break;
        case kFilterParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kFilterParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kFilterParamDepth: fDepth.setTarget(value / 100.0f); break;
        case kFilterParamMix:   fMix.setTarget(value / 100.0f); break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateRate();
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fMix.snap(fMix.target());
            fCutoff.snap(fCutoff.target());
        }
        static constexpr LfoShape shapes[] = { LfoShape::Sine, LfoShape::Triangle, LfoShape::SawUp, LfoShape::SawDown,
                                               LfoShape::Square };
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double previous = fLfo.phase();
            const double phase = fLfo.advance();
            float lfo;
            if (fShape == 5)
            {
                // a new random step each cycle
                if (phase < previous)
                    fStep = random();
                fStepSmooth.setTarget(fStep);
                lfo = fStepSmooth.next();
            }
            else
            {
                lfo = Lfo::shape(shapes[fShape], phase);
            }
            const double freq = fCutoff.next() * std::exp2(fDepth.next() * kDepthOctaves * lfo);
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                fFilter[ch].set(fSampleRate, std::min(freq, 0.45 * fSampleRate), fQ);
                const float x = io[ch][i];
                const Svf::Out out = fFilter[ch].process(x);
                float y;
                switch (fType)
                {
                case 1:  y = out.band * fFilter[ch].k(); break;
                case 2:  y = out.high; break;
                case 3:  y = x - out.band * fFilter[ch].k(); break; // notch: everything but the band
                default: y = out.low; break;
                }
                io[ch][i] = x + (y - x) * mix;
            }
        }
    }

    void reset() override
    {
        fFilter[0].reset();
        fFilter[1].reset();
        fLfo.reset();
        fStep = 0.0f;
        fStepSmooth.snap(0.0f);
        fSnap = true;
    }

private:
    float random()
    {
        // xorshift32, -1..1
        fRandom ^= fRandom << 13;
        fRandom ^= fRandom >> 17;
        fRandom ^= fRandom << 5;
        return static_cast<float>(fRandom) / 2147483648.0f - 1.0f;
    }

    void updateRate()
    {
        const ParamSpec& spec = kFilterBlockDescriptor.params[kFilterParamRate];
        fLfo.setRate(fSync ? noteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    int fType = 0;
    float fQ = 4.0f;
    int fShape = 0;
    float fRateHz = 1.0f;
    bool fSync = false;
    Lfo fLfo;
    Svf fFilter[2];
    Smoother fDepth, fMix, fCutoff, fStepSmooth;
    float fStep = 0.0f;
    uint32_t fRandom = 0x2545f491u;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kFilterBlockDescriptor = {
    "filter",
    "Filter",
    Category::Filter,
    kFilterParamCount,
    {
        ParamSpec { "Type", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Cutoff", "Hz", 60.0f, 8000.0f, 800.0f, ParamScale::Log },
        ParamSpec { "Resonance", "", 0.5f, 15.0f, 4.0f, ParamScale::Log },
        ParamSpec { "Shape", "", 0.0f, 5.0f, 0.0f, ParamScale::Choice, kShapeNames },
        ParamSpec { "Rate", "Hz", 0.05f, 15.0f, 1.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new FilterBlock()); },
    kFilterParamSync,
    kFilterParamRate,
    true,
};

} // namespace namsayin
