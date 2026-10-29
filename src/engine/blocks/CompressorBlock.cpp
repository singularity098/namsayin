// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/CompressorBlock.hpp"

#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr float kDbPerLog2 = 6.0205999f; // 20 * log10(2)

class CompressorBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMakeup.prepare(sampleRate);
        fMix.prepare(sampleRate);
        fMakeup.snap(fMakeupDb);
        fMix.snap(fMixValue);
        updateTimes();
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kCompParamThreshold:
            fThresholdDb = value;
            break;
        case kCompParamRatio:
            fRatio = std::max(1.0f, value);
            break;
        case kCompParamAttack:
            fAttackMs = value;
            updateTimes();
            break;
        case kCompParamRelease:
            fReleaseMs = value;
            updateTimes();
            break;
        case kCompParamKnee:
            fKneeDb = value;
            break;
        case kCompParamMakeup:
            fMakeupDb = value;
            fMakeup.setTarget(value);
            break;
        case kCompParamMix:
            fMixValue = value / 100.0f;
            fMix.setTarget(fMixValue);
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            // first buffer after prepare()/reset(): start at the parameter values instead of gliding to them
            fSnap = false;
            fMakeup.snap(fMakeup.target());
            fMix.snap(fMix.target());
        }

        float maxReduction = 0.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            float peak = std::fabs(io[0][i]);
            if (numChannels > 1)
                peak = std::max(peak, std::fabs(io[1][i]));

            const float inDb = peak > 1e-6f ? kDbPerLog2 * std::log2(peak) : -120.0f;
            const float target = compressorCurve(inDb, fThresholdDb, fRatio, fKneeDb) - inDb; // <= 0

            // more reduction: attack; less: release
            const float coef = target < fGainDb ? fAttackCoef : fReleaseCoef;
            fGainDb = target + (fGainDb - target) * coef;
            maxReduction = std::max(maxReduction, -fGainDb);

            const float gain = std::exp2((fGainDb + fMakeup.next()) / kDbPerLog2);
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float dry = io[ch][i];
                io[ch][i] = dry + (dry * gain - dry) * mix;
            }
        }
        fMeter = maxReduction;
    }

    float meter() const override { return fMeter; }

    void reset() override
    {
        fGainDb = 0.0f;
        fMeter = 0.0f;
        fSnap = true;
    }

private:
    void updateTimes()
    {
        fAttackCoef = static_cast<float>(std::exp(-1.0 / (std::max(0.01f, fAttackMs) / 1000.0 * fSampleRate)));
        fReleaseCoef = static_cast<float>(std::exp(-1.0 / (std::max(1.0f, fReleaseMs) / 1000.0 * fSampleRate)));
    }

    double fSampleRate = 48000.0;
    float fThresholdDb = -20.0f;
    float fRatio = 4.0f;
    float fAttackMs = 10.0f;
    float fReleaseMs = 150.0f;
    float fKneeDb = 6.0f;
    float fMakeupDb = 0.0f;
    float fMixValue = 1.0f;

    float fAttackCoef = 0.0f;
    float fReleaseCoef = 0.0f;
    float fGainDb = 0.0f; // smoothed gain change, <= 0
    float fMeter = 0.0f;
    bool fSnap = true;
    Smoother fMakeup, fMix;
};

} // namespace

float compressorCurve(float x, float threshold, float ratio, float knee)
{
    const float over = x - threshold;
    if (2.0f * over < -knee)
        return x;
    if (knee > 0.0f && 2.0f * std::fabs(over) <= knee)
    {
        const float t = over + knee / 2.0f;
        return x + (1.0f / ratio - 1.0f) * t * t / (2.0f * knee);
    }
    return threshold + over / ratio;
}

const BlockDescriptor kCompressorBlockDescriptor = {
    "compressor",
    "Compressor",
    Category::Dynamics,
    kCompParamCount,
    {
        ParamSpec { "Threshold", "dB", -60.0f, 0.0f, -20.0f, ParamScale::Linear },
        ParamSpec { "Ratio", ":1", 1.0f, 20.0f, 4.0f, ParamScale::Log },
        ParamSpec { "Attack", "ms", 0.1f, 100.0f, 10.0f, ParamScale::Log },
        ParamSpec { "Release", "ms", 10.0f, 2000.0f, 150.0f, ParamScale::Log },
        ParamSpec { "Knee", "dB", 0.0f, 24.0f, 6.0f, ParamScale::Linear },
        ParamSpec { "Makeup", "dB", 0.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new CompressorBlock()); },
    -1,
    -1,
    false,
    "Reduction",
};

} // namespace xythos
