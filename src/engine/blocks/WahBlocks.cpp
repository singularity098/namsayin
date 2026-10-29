// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/WahBlocks.hpp"

#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

// A wah's voice: a resonant peak over attenuated lows (about 12 dB below the peak at the default resonance), made
// up to roughly the bypassed level
float wahVoice(const Svf::Out& out, float k)
{
    return 2.0f * (k * out.band + 0.3f * out.low);
}

constexpr const char* kWahTypeNames[] = { "Classic", "Vox", "Wide", "Bass" };

struct WahVoicing {
    float low, high, q;
};

constexpr WahVoicing kWahVoicings[] = {
    { 350.0f, 2200.0f, 5.0f }, // Classic
    { 450.0f, 1700.0f, 4.0f }, // Vox
    { 250.0f, 3200.0f, 6.0f }, // Wide
    { 150.0f, 1200.0f, 4.0f }, // Bass
};

class WahBlock : public Block {
public:
    WahBlock()
    {
        fPosition.snap(0.5f);
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fPosition.prepare(sampleRate, 0.015);
        fLevel.prepare(sampleRate);
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kWahParamPosition:
            fPosition.setTarget(value / 100.0f);
            break;
        case kWahParamType:
            fType = static_cast<WahType>(std::clamp(static_cast<int>(value), 0, 3));
            break;
        case kWahParamResonance:
            fResonance = value / 100.0f;
            break;
        case kWahParamLevel:
            fLevel.setTarget(dbToGain(value));
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fPosition.snap(fPosition.target());
            fLevel.snap(fLevel.target());
        }

        const WahVoicing& v = kWahVoicings[static_cast<int>(fType)];
        const double q = v.q * std::exp2(2.0 * (fResonance - 0.5)); // half to double the type's
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float position = fPosition.next();
            const float level = fLevel.next();
            const double freq = v.low * std::pow(static_cast<double>(v.high / v.low), position);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                fFilter[ch].set(fSampleRate, freq, q);
                io[ch][i] = wahVoice(fFilter[ch].process(io[ch][i]), fFilter[ch].k()) * level;
            }
        }
    }

    void reset() override
    {
        fFilter[0].reset();
        fFilter[1].reset();
        fSnap = true;
    }

private:
    double fSampleRate = 48000.0;
    WahType fType = WahType::Classic;
    float fResonance = 0.5f;
    Svf fFilter[2];
    Smoother fPosition, fLevel;
    bool fSnap = true;
};

constexpr const char* kDirectionNames[] = { "Up", "Down" };
constexpr const char* kFilterNames[] = { "Wah", "Low-pass", "Band-pass", "High-pass" };

constexpr double kAutoWahAttackMs = 4.0;

class AutoWahBlock : public Block {
public:
    AutoWahBlock()
    {
        fMix.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        updateEnvelope();
        // the filter position glides a little, so a fast envelope doesn't make the sweep buzz
        fPosition.prepare(sampleRate, 0.004);
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kAutoWahParamSensitivity:
            fSensitivity = dbToGain(value);
            break;
        case kAutoWahParamDecay:
            fDecayMs = value;
            updateEnvelope();
            break;
        case kAutoWahParamLow:
            fLow = value;
            break;
        case kAutoWahParamHigh:
            fHigh = value;
            break;
        case kAutoWahParamResonance:
            fQ = value;
            break;
        case kAutoWahParamDirection:
            fDown = value >= 0.5f;
            break;
        case kAutoWahParamFilter:
            fFilterType = static_cast<AutoWahFilter>(std::clamp(static_cast<int>(value), 0, 3));
            break;
        case kAutoWahParamMix:
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

        const double ratio = static_cast<double>(fHigh) / fLow;
        for (uint32_t i = 0; i < frames; ++i)
        {
            float in = io[0][i];
            if (numChannels > 1)
                in = 0.5f * (in + io[1][i]);
            const float env = fEnvelope.process(in);
            fPosition.setTarget(std::min(1.0f, env * fSensitivity));
            float position = fPosition.next();
            if (fDown)
                position = 1.0f - position;
            const double freq = fLow * std::pow(ratio, static_cast<double>(position));
            const float mix = fMix.next();

            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                fFilter[ch].set(fSampleRate, freq, fQ);
                const float x = io[ch][i];
                const Svf::Out out = fFilter[ch].process(x);
                float wet;
                switch (fFilterType)
                {
                case AutoWahFilter::LowPass:  wet = out.low; break;
                case AutoWahFilter::BandPass: wet = out.band * fFilter[ch].k(); break;
                case AutoWahFilter::HighPass: wet = out.high; break;
                default:                      wet = wahVoice(out, fFilter[ch].k()); break;
                }
                io[ch][i] = x + (wet - x) * mix;
            }
        }
    }

    void reset() override
    {
        fFilter[0].reset();
        fFilter[1].reset();
        fEnvelope.reset();
        fPosition.snap(0.0f);
        fSnap = true;
    }

private:
    void updateEnvelope() { fEnvelope.prepare(fSampleRate, kAutoWahAttackMs, fDecayMs); }

    double fSampleRate = 48000.0;
    float fSensitivity = 10.0f;
    float fDecayMs = 150.0f;
    float fLow = 300.0f;
    float fHigh = 2500.0f;
    float fQ = 5.0f;
    bool fDown = false;
    AutoWahFilter fFilterType = AutoWahFilter::Wah;
    Svf fFilter[2];
    EnvelopeFollower fEnvelope;
    Smoother fPosition, fMix;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kWahBlockDescriptor = {
    "wah",
    "Wah",
    Category::Wah,
    kWahParamCount,
    {
        ParamSpec { "Position", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kWahTypeNames },
        ParamSpec { "Resonance", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new WahBlock()); },
};

const BlockDescriptor kAutoWahBlockDescriptor = {
    "autowah",
    "Auto Wah",
    Category::Wah,
    kAutoWahParamCount,
    {
        ParamSpec { "Sensitivity", "dB", 0.0f, 40.0f, 20.0f, ParamScale::Linear },
        ParamSpec { "Decay", "ms", 20.0f, 1000.0f, 150.0f, ParamScale::Log },
        ParamSpec { "Low", "Hz", 100.0f, 1500.0f, 300.0f, ParamScale::Log },
        ParamSpec { "High", "Hz", 500.0f, 5000.0f, 2500.0f, ParamScale::Log },
        ParamSpec { "Resonance", "", 0.7f, 15.0f, 5.0f, ParamScale::Log },
        ParamSpec { "Direction", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kDirectionNames },
        ParamSpec { "Filter", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kFilterNames },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new AutoWahBlock()); },
};

} // namespace xythos
