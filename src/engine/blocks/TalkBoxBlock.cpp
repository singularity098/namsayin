// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/TalkBoxBlock.hpp"

#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

// Vowel formants (adult male voice, after Peterson and Barney)
constexpr Formants kU { 300.0f, 870.0f, 2240.0f };
constexpr Formants kO { 570.0f, 840.0f, 2410.0f };
constexpr Formants kA { 730.0f, 1090.0f, 2440.0f };
constexpr Formants kE { 530.0f, 1840.0f, 2480.0f };
constexpr Formants kI { 270.0f, 2290.0f, 3010.0f };

constexpr const char* kPathNames[] = { "Oo-Ah", "Ee-Ah", "Oo-Ee", "A-E-I-O-U" };
struct Path {
    const Formants* points[5];
    int count;
};
constexpr Path kPaths[] = {
    { { &kU, &kO, &kA }, 3 },
    { { &kI, &kE, &kA }, 3 },
    { { &kU, &kA, &kI }, 3 },
    { { &kA, &kE, &kI, &kO, &kU }, 5 },
};
constexpr int kPathCount = sizeof(kPaths) / sizeof(kPaths[0]);
constexpr const char* kSourceNames[] = { "Pedal", "Envelope", "LFO" };
constexpr float kFormantLevels[3] = { 1.0f, 0.7f, 0.35f };
constexpr float kMakeup = 2.0f; // narrow bands pass less than the whole guitar

float lerpLog(float a, float b, float t)
{
    return a * std::pow(b / a, t);
}

class TalkBoxBlock : public Block {
public:
    TalkBoxBlock()
    {
        fPosition.snap(0.5f);
        fMix.snap(1.0f);
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fPosition.prepare(sampleRate, 0.015);
        fMix.prepare(sampleRate);
        fLevel.prepare(sampleRate);
        fEnvelope.prepare(sampleRate, 5.0, 150.0);
        fLfo.prepare(sampleRate);
        fLfo.setRate(fRateHz);
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kTalkParamPosition:    fKnob = value / 100.0f; break;
        case kTalkParamVowels:      fPath = std::clamp(static_cast<int>(value), 0, kPathCount - 1); break;
        case kTalkParamSource:      fSource = std::clamp(static_cast<int>(value), 0, 2); break;
        case kTalkParamSensitivity: fSensitivity = value / 100.0f; break;
        case kTalkParamRate:
            fRateHz = value;
            fLfo.setRate(fRateHz);
            break;
        case kTalkParamResonance: fQ = 4.0f + 16.0f * value / 100.0f; break;
        case kTalkParamMix:       fMix.setTarget(value / 100.0f); break;
        case kTalkParamLevel:     fLevel.setTarget(dbToGain(value)); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fPosition.snap(target(0.0f));
            fMix.snap(fMix.target());
            fLevel.snap(fLevel.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            float in = std::fabs(io[0][i]);
            if (numChannels > 1)
                in = std::max(in, std::fabs(io[1][i]));
            fPosition.setTarget(target(in));
            const Formants f = talkBoxFormants(fPath, fPosition.next());
            const float freqs[3] = { f.f1, f.f2, f.f3 };
            const float mix = fMix.next();
            const float level = fLevel.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                float y = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    Svf& filter = fFilters[ch][k];
                    filter.set(fSampleRate, freqs[k], fQ);
                    // the band output times k peaks at unity
                    y += filter.process(x).band * filter.k() * kFormantLevels[k];
                }
                io[ch][i] = (x + (y * kMakeup - x) * mix) * level;
            }
        }
    }

    void reset() override
    {
        for (auto& channel : fFilters)
            for (Svf& filter : channel)
                filter.reset();
        fEnvelope.reset();
        fLfo.reset();
        fSnap = true;
    }

private:
    // Where along the vowels, from the source
    float target(float in)
    {
        switch (fSource)
        {
        case 1: {
            // Position is where it rests; playing harder opens it towards the end of the path
            const float env = fEnvelope.process(in);
            const float envDb = env > 1e-7f ? 20.0f * std::log10(env) : -140.0f;
            const float topDb = -6.0f - 36.0f * fSensitivity;
            const float amount = std::clamp((envDb - (topDb - 30.0f)) / 30.0f, 0.0f, 1.0f);
            return fKnob + (1.0f - fKnob) * amount;
        }
        case 2:
            // sweeps between the start of the path and Position
            return fKnob * (0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(fLfo.advance())));
        default:
            return fKnob;
        }
    }

    double fSampleRate = 48000.0;
    float fKnob = 0.5f;
    int fPath = 0;
    int fSource = 0;
    float fSensitivity = 0.5f;
    float fRateHz = 1.0f;
    float fQ = 12.0f;
    Svf fFilters[2][3];
    EnvelopeFollower fEnvelope;
    Lfo fLfo;
    Smoother fPosition, fMix, fLevel;
    bool fSnap = true;
};

} // namespace

Formants talkBoxFormants(int path, float position)
{
    const Path& p = kPaths[std::clamp(path, 0, kPathCount - 1)];
    const float x = std::clamp(position, 0.0f, 1.0f) * static_cast<float>(p.count - 1);
    const int i = std::min(static_cast<int>(x), p.count - 2);
    const float t = x - static_cast<float>(i);
    const Formants& a = *p.points[i];
    const Formants& b = *p.points[i + 1];
    return { lerpLog(a.f1, b.f1, t), lerpLog(a.f2, b.f2, t), lerpLog(a.f3, b.f3, t) };
}

const BlockDescriptor kTalkBoxBlockDescriptor = {
    "talkbox",
    "Talk Box",
    Category::Wah,
    kTalkParamCount,
    {
        ParamSpec { "Position", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Vowels", "", 0.0f, static_cast<float>(kPathCount - 1), 0.0f, ParamScale::Choice, kPathNames },
        ParamSpec { "Source", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kSourceNames },
        ParamSpec { "Sensitivity", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Rate", "Hz", 0.1f, 10.0f, 1.0f, ParamScale::Log },
        ParamSpec { "Resonance", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new TalkBoxBlock()); },
};

} // namespace xythos
