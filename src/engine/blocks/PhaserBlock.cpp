// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/PhaserBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kTypeNames[] = { "4 Stage", "6 Stage", "8 Stage", "12 Stage", "Vibe" };
constexpr int kStageCounts[] = { 4, 6, 8, 12, 4 };
constexpr int kMaxStages = 12;
constexpr float kDepthOctaves = 2.5f; // each way at 100%
// Vibe: the four stages sit at fixed ratios of the sweep frequency (its four phase-shift capacitors differ)
constexpr float kVibeRatios[] = { 0.3f, 0.75f, 1.6f, 4.0f };

class PhaserBlock : public Block {
public:
    PhaserBlock()
    {
        fDepth.snap(0.7f);
        fCenter.snap(700.0f);
        fFeedback.snap(0.3f);
        fMix.snap(0.5f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.05);
        fCenter.prepare(sampleRate, 0.05);
        fFeedback.prepare(sampleRate);
        fMix.prepare(sampleRate);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kPhaserParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kPhaserParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kPhaserParamDepth:
            fDepth.setTarget(value / 100.0f);
            break;
        case kPhaserParamCenter:
            fCenter.setTarget(value);
            break;
        case kPhaserParamFeedback:
            fFeedback.setTarget(std::clamp(value, -90.0f, 90.0f) / 100.0f);
            break;
        case kPhaserParamMix:
            fMix.setTarget(value / 100.0f);
            break;
        case kPhaserParamType:
            fType = static_cast<PhaserType>(std::clamp(static_cast<int>(value), 0, 4));
            break;
        case kPhaserParamWidth:
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
            const ParamSpec& spec = kPhaserBlockDescriptor.params[kPhaserParamRate];
            fLfo.syncTo(songPhase(beats, rateNoteBeats(rateNoteIndex(spec.toNormalized(fRateHz)))) + 0.75);
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fCenter.snap(fCenter.target());
            fFeedback.snap(fFeedback.target());
            fMix.snap(fMix.target());
        }

        const bool vibe = fType == PhaserType::Vibe;
        const int stages = kStageCounts[static_cast<int>(fType)];
        const double piOverRate = M_PI / fSampleRate;
        const double maxFreq = 0.45 * fSampleRate;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const double phase = fLfo.advance();
            const float depth = fDepth.next();
            const float center = fCenter.next();
            const float feedback = vibe ? 0.0f : fFeedback.next();
            const float mix = fMix.next();

            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const double p = phase + (ch == 1 ? 0.5 * fWidth : 0.0);
                float lfo;
                if (vibe)
                {
                    // a lamp's glow: lingers dark and flashes bright (a skewed, squared sine)
                    const float s = 0.5f + 0.5f * Lfo::shape(LfoShape::Sine, p);
                    lfo = 2.0f * s * s - 1.0f;
                }
                else
                {
                    lfo = Lfo::shape(LfoShape::Triangle, p);
                }
                const double freq = center * std::exp2(depth * kDepthOctaves * lfo);

                float y = io[ch][i] + feedback * fLast[ch];
                if (vibe)
                {
                    for (int s = 0; s < stages; ++s)
                        y = allpass(ch, s, coefficient(std::min(freq * kVibeRatios[s], maxFreq), piOverRate), y);
                }
                else
                {
                    const float a = coefficient(std::min(freq, maxFreq), piOverRate);
                    for (int s = 0; s < stages; ++s)
                        y = allpass(ch, s, a, y);
                }
                fLast[ch] = std::clamp(y, -4.0f, 4.0f);

                const float x = io[ch][i];
                // the notches take about 3 dB off at 50%; make that up
                const float makeup = 1.0f + 1.66f * mix * (1.0f - mix);
                io[ch][i] = (x * (1.0f - mix) + y * mix) * makeup;
            }
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (float& z : fState[ch])
                z = 0.0f;
            fLast[ch] = 0.0f;
        }
        fLfo.reset(0.75);
        fSnap = true;
    }

private:
    static float coefficient(double freq, double piOverRate)
    {
        const double t = std::tan(freq * piOverRate);
        return static_cast<float>((t - 1.0) / (t + 1.0));
    }

    float allpass(uint32_t ch, int stage, float a, float x)
    {
        float& z = fState[ch][stage];
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }

    void updateRate()
    {
        const ParamSpec& spec = kPhaserBlockDescriptor.params[kPhaserParamRate];
        fLfo.setRate(fSync ? rateNoteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fRateHz = 0.5f;
    bool fSync = false;
    PhaserType fType = PhaserType::Stages4;
    float fWidth = 0.5f;
    Lfo fLfo;
    Smoother fDepth, fCenter, fFeedback, fMix;
    float fState[2][kMaxStages] {};
    float fLast[2] {};
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kPhaserBlockDescriptor = {
    "phaser",
    "Phaser",
    Category::Modulation,
    kPhaserParamCount,
    {
        ParamSpec { "Rate", "Hz", 0.02f, 10.0f, 0.5f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Center", "Hz", 100.0f, 4000.0f, 700.0f, ParamScale::Log },
        ParamSpec { "Feedback", "%", -90.0f, 90.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 4.0f, 0.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new PhaserBlock()); },
    kPhaserParamSync,
    kPhaserParamRate,
    true,
};

} // namespace xythos
