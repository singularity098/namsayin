// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/FlangerBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr double kMaxDelayMs = 40.0;
constexpr float kDepthOctaves = 2.0f;

class FlangerBlock : public Block {
public:
    FlangerBlock()
    {
        fDepth.snap(0.6f);
        fManual.snap(1.5f);
        fFeedback.snap(0.5f);
        fMix.snap(0.5f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(std::ceil(kMaxDelayMs / 1000.0 * sampleRate)) + 4);
        fLfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.05);
        fManual.prepare(sampleRate, 0.05);
        fFeedback.prepare(sampleRate);
        fMix.prepare(sampleRate);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kFlangerParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kFlangerParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kFlangerParamDepth:
            fDepth.setTarget(value / 100.0f);
            break;
        case kFlangerParamManual:
            fManual.setTarget(value);
            break;
        case kFlangerParamFeedback:
            fFeedback.setTarget(std::clamp(value, -95.0f, 95.0f) / 100.0f);
            break;
        case kFlangerParamMix:
            fMix.setTarget(value / 100.0f);
            break;
        case kFlangerParamWidth:
            fWidth = value / 100.0f;
            break;
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

    void setSongPosition(double beats) override
    {
        // synced: lock the LFO to the song, so the sweep starts from its bottom on the beat
        if (fSync)
        {
            const ParamSpec& spec = kFlangerBlockDescriptor.params[kFlangerParamRate];
            fLfo.syncTo(songPhase(beats, rateNoteBeats(rateNoteIndex(spec.toNormalized(fRateHz)))) + 0.75);
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fManual.snap(fManual.target());
            fFeedback.snap(fFeedback.target());
            fMix.snap(fMix.target());
        }

        const float msToSamples = static_cast<float>(fSampleRate / 1000.0);
        const float maxDelay = static_cast<float>(fLines[0].capacity() - 4);
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double phase = fLfo.advance();
            const float depth = fDepth.next();
            const float manual = fManual.next();
            const float feedback = fFeedback.next();
            const float mix = fMix.next();
            // equal parts at 50%, like a classic flanger; the comb's notches take about 3 dB off there, which is
            // made up, and feedback's boost is tamed
            const float makeup = 1.0f + 1.66f * mix * (1.0f - mix);
            const float wetGain = mix * (1.0f - 0.5f * std::fabs(feedback)) * makeup;
            const float dryGain = (1.0f - mix) * makeup;

            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const double p = phase + (ch == 1 ? 0.5 * fWidth : 0.0);
                const float lfo = Lfo::shape(LfoShape::Triangle, p);
                const float delayMs = manual * std::exp2(depth * kDepthOctaves * lfo);
                const float delay = std::clamp(delayMs * msToSamples, 1.5f, maxDelay);
                const float x = io[ch][i];
                const float delayed = fLines[ch].readFractional(delay);
                const float wet = x + feedback * delayed;
                // keep runaway feedback bounded
                fLines[ch].write(std::clamp(wet, -4.0f, 4.0f));
                io[ch][i] = x * dryGain + delayed * wetGain;
            }
        }
    }

    void reset() override
    {
        for (DelayLine& line : fLines)
            line.reset();
        fLfo.reset(0.75); // start at the bottom of the sweep
        fSnap = true;
    }

private:
    void updateRate()
    {
        const ParamSpec& spec = kFlangerBlockDescriptor.params[kFlangerParamRate];
        fLfo.setRate(fSync ? rateNoteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fRateHz = 0.3f;
    bool fSync = false;
    float fWidth = 0.5f;
    DelayLine fLines[2];
    Lfo fLfo;
    Smoother fDepth, fManual, fFeedback, fMix;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kFlangerBlockDescriptor = {
    "flanger",
    "Flanger",
    Category::Modulation,
    kFlangerParamCount,
    {
        ParamSpec { "Rate", "Hz", 0.02f, 10.0f, 0.3f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Manual", "ms", 0.2f, 8.0f, 1.5f, ParamScale::Log },
        ParamSpec { "Feedback", "%", -95.0f, 95.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new FlangerBlock()); },
    kFlangerParamSync,
    kFlangerParamRate,
    true,
};

} // namespace namsayin
