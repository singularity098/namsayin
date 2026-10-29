// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/BarberPoleBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kDirectionNames[] = { "Up", "Down" };
constexpr const char* kStageNames[] = { "4 Stage", "6 Stage", "8 Stage" };
constexpr int kStageCounts[] = { 4, 6, 8 };
constexpr int kMaxStages = 8;
constexpr int kVoices = 3;
constexpr double kLowHz = 60.0;
constexpr double kOctaves = 6.0; // each sweep climbs from 60 Hz to about 3.8 kHz

class BarberPoleBlock : public Block {
public:
    BarberPoleBlock()
    {
        fFeedback.snap(0.4f);
        fMix.snap(0.5f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLfo.prepare(sampleRate);
        fFeedback.prepare(sampleRate);
        fMix.prepare(sampleRate);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kBarberParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kBarberParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kBarberParamDirection: fDown = value >= 0.5f; break;
        case kBarberParamStages:    fStages = kStageCounts[std::clamp(static_cast<int>(value), 0, 2)]; break;
        case kBarberParamFeedback:  fFeedback.setTarget(value / 100.0f); break;
        case kBarberParamWidth:     fWidth = value / 100.0f; break;
        case kBarberParamMix:       fMix.setTarget(value / 100.0f); break;
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
        if (fSync)
        {
            const ParamSpec& spec = kBarberPoleBlockDescriptor.params[kBarberParamRate];
            fLfo.syncTo(songPhase(beats, rateNoteBeats(rateNoteIndex(spec.toNormalized(fRateHz)))));
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fFeedback.snap(fFeedback.target());
            fMix.snap(fMix.target());
        }
        const double piOverRate = M_PI / fSampleRate;
        const double maxFreq = 0.45 * fSampleRate;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double phase = fLfo.advance();
            const float feedback = fFeedback.next();
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                const double offset = ch == 1 ? 0.5 * fWidth / kVoices : 0.0;
                float y = 0.0f;
                for (int v = 0; v < kVoices; ++v)
                {
                    double p = phase + offset + static_cast<double>(v) / kVoices;
                    p -= std::floor(p);
                    const double sweep = fDown ? 1.0 - p : p;
                    // each voice is loudest mid-sweep and silent at the ends, where it jumps back: the three
                    // raised-cosine weights a third apart add up to 1.5
                    const auto s = static_cast<float>(std::sin(M_PI * p));
                    const float weight = s * s / 1.5f;
                    const float a = coefficient(std::min(kLowHz * std::exp2(kOctaves * sweep), maxFreq), piOverRate);
                    float z = x + feedback * fLast[ch][v];
                    for (int st = 0; st < fStages; ++st)
                        z = allpass(ch, v, st, a, z);
                    fLast[ch][v] = std::clamp(z, -4.0f, 4.0f);
                    y += weight * z;
                }
                // the notches take about 3 dB off at 50%; make that up (as the phaser does)
                const float makeup = 1.0f + 1.66f * mix * (1.0f - mix);
                io[ch][i] = (x * (1.0f - mix) + y * mix) * makeup;
            }
        }
    }

    void reset() override
    {
        for (auto& channel : fState)
            for (auto& voice : channel)
                for (float& z : voice)
                    z = 0.0f;
        for (auto& channel : fLast)
            for (float& z : channel)
                z = 0.0f;
        fLfo.reset();
        fSnap = true;
    }

private:
    static float coefficient(double freq, double piOverRate)
    {
        const double t = std::tan(freq * piOverRate);
        return static_cast<float>((t - 1.0) / (t + 1.0));
    }

    float allpass(uint32_t ch, int voice, int stage, float a, float x)
    {
        float& z = fState[ch][voice][stage];
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }

    void updateRate()
    {
        const ParamSpec& spec = kBarberPoleBlockDescriptor.params[kBarberParamRate];
        fLfo.setRate(fSync ? rateNoteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fRateHz = 0.1f;
    bool fSync = false;
    bool fDown = false;
    int fStages = 6;
    float fWidth = 0.5f;
    Lfo fLfo;
    Smoother fFeedback, fMix;
    float fState[2][kVoices][kMaxStages] {};
    float fLast[2][kVoices] {};
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kBarberPoleBlockDescriptor = {
    "barberpole",
    "Barber Pole",
    Category::Modulation,
    kBarberParamCount,
    {
        ParamSpec { "Rate", "Hz", 0.02f, 2.0f, 0.1f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Direction", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kDirectionNames },
        ParamSpec { "Stages", "", 0.0f, 2.0f, 1.0f, ParamScale::Choice, kStageNames },
        ParamSpec { "Feedback", "%", 0.0f, 80.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new BarberPoleBlock()); },
    kBarberParamSync,
    kBarberParamRate,
    true,
};

} // namespace xythos
