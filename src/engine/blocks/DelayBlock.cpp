// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/DelayBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kModeNames[] = { "Mono", "Stereo", "Ping-Pong" };

constexpr float kMinDelayMs = 1.0f;
constexpr float kMaxDelayMs = 2000.0f;
constexpr double kModRateHz = 0.6;
constexpr double kModMaxMs = 3.0;
constexpr double kTimeSmoothingSeconds = 0.12;

// Keeps runaway feedback bounded (below 1.0): linear up to 0.5, then a smooth tanh knee
float saturate(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.5f)
        return x;
    return std::copysign(0.5f + 0.5f * std::tanh(2.0f * (a - 0.5f)), x);
}

class DelayBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        const size_t maxDelay =
            static_cast<size_t>(std::ceil((kDelayMaxSeconds + (kModMaxMs + 60.0) / 1000.0) * sampleRate));
        for (DelayLine& line : fLines)
            line.prepare(maxDelay);
        fTime[0].prepare(sampleRate, kTimeSmoothingSeconds);
        fTime[1].prepare(sampleRate, kTimeSmoothingSeconds);
        fMix.prepare(sampleRate);
        fFeedback.prepare(sampleRate);
        fLfo[0].prepare(sampleRate, kModRateHz, 0.0);
        fLfo[1].prepare(sampleRate, kModRateHz, M_PI / 2.0);
        updateFilters();
        updateTargets();
        fTime[0].snap(fTime[0].target());
        fTime[1].snap(fTime[1].target());
        fMix.snap(fMixValue);
        fFeedback.snap(fFeedbackValue);
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kDelayParamTime:
            fTimeMs = value;
            break;
        case kDelayParamSync:
            fSync = value >= 0.5f;
            break;
        case kDelayParamFeedback:
            fFeedbackValue = value / 100.0f;
            break;
        case kDelayParamMix:
            fMixValue = value / 100.0f;
            break;
        case kDelayParamTone:
            fTone = value;
            updateFilters();
            break;
        case kDelayParamMode:
            fMode = static_cast<DelayMode>(std::clamp(static_cast<int>(value), 0, 2));
            break;
        case kDelayParamSpread:
            fSpreadMs = value;
            break;
        case kDelayParamMod:
            fModDepth = value / 100.0f;
            break;
        }
        updateTargets();
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateTargets();
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            // first buffer after prepare()/reset(): start at the parameter values instead of gliding to them
            fSnap = false;
            fTime[0].snap(fTime[0].target());
            fTime[1].snap(fTime[1].target());
            fMix.snap(fMix.target());
            fFeedback.snap(fFeedback.target());
        }

        const float modSamples = static_cast<float>(fModDepth * kModMaxMs / 1000.0 * fSampleRate);
        const float maxDelay = static_cast<float>(fLines[0].capacity() - 4);

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mix = fMix.next();
            const float feedback = fFeedback.next();
            const float dryGain = dryGainForMix(mix);
            const float wetGain = wetGainForMix(mix);

            float delays[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float mod = modSamples > 0.0f ? (1.0f + fLfo[ch].next()) * 0.5f * modSamples : 0.0f;
                delays[ch] = std::clamp(fTime[ch].next() + mod, 1.0f, maxDelay);
            }

            if (numChannels == 1)
            {
                const float x = io[0][i];
                const float wet = fLines[0].readFractional(delays[0]);
                fLines[0].write(x + saturate(filter(0, wet) * feedback));
                io[0][i] = x * dryGain + wet * wetGain;
                continue;
            }

            const float inL = io[0][i];
            const float inR = io[1][i];
            const float wetL = fLines[0].readFractional(delays[0]);
            const float wetR = fLines[1].readFractional(delays[1]);
            const float fbL = filter(0, wetL) * feedback;
            const float fbR = filter(1, wetR) * feedback;

            switch (fMode)
            {
            case DelayMode::Mono: {
                const float x = 0.5f * (inL + inR);
                fLines[0].write(x + saturate(fbL));
                fLines[1].write(x + saturate(fbR));
                const float wet = 0.5f * (wetL + wetR);
                io[0][i] = inL * dryGain + wet * wetGain;
                io[1][i] = inR * dryGain + wet * wetGain;
                break;
            }
            case DelayMode::Stereo:
                fLines[0].write(inL + saturate(fbL));
                fLines[1].write(inR + saturate(fbR));
                io[0][i] = inL * dryGain + wetL * wetGain;
                io[1][i] = inR * dryGain + wetR * wetGain;
                break;
            case DelayMode::PingPong:
                // the input enters on the left; repeats bounce left -> right -> left
                fLines[0].write(0.5f * (inL + inR) + saturate(fbR));
                fLines[1].write(saturate(fbL));
                io[0][i] = inL * dryGain + wetL * wetGain;
                io[1][i] = inR * dryGain + wetR * wetGain;
                break;
            }
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fLines[ch].reset();
            fToneFilter[ch].reset();
            fLowCut[ch].reset();
        }
        fSnap = true;
    }

private:
    float filter(int ch, float x)
    {
        x = fLowCut[ch].process(x);
        return fToneOn ? fToneFilter[ch].process(x) : x;
    }

    void updateFilters()
    {
        fToneOn = fTone < 19900.0f;
        const double nyquistSafe = 0.45 * fSampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            fToneFilter[ch].setLowPass(fSampleRate, std::min<double>(fTone, nyquistSafe));
            fLowCut[ch].setHighPass(fSampleRate, 40.0);
        }
    }

    void updateTargets()
    {
        double ms = fTimeMs;
        if (fSync)
        {
            const ParamSpec& spec = kDelayBlockDescriptor.params[kDelayParamTime];
            ms = delayNoteBeats(delayNoteIndex(spec.toNormalized(fTimeMs))) * 60000.0 / fBpm;
        }
        const double spread = fMode == DelayMode::Mono ? 0.0 : fSpreadMs;
        const double maxMs = kDelayMaxSeconds * 1000.0;
        fTime[0].setTarget(static_cast<float>(std::min(ms, maxMs) / 1000.0 * fSampleRate));
        fTime[1].setTarget(static_cast<float>(std::min(ms + spread, maxMs) / 1000.0 * fSampleRate));
        fMix.setTarget(fMixValue);
        fFeedback.setTarget(fFeedbackValue);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    DelayLine fLines[2];
    Smoother fTime[2], fMix, fFeedback;
    SineLfo fLfo[2];
    Biquad fToneFilter[2], fLowCut[2];

    float fTimeMs = 400.0f;
    bool fSync = false;
    float fFeedbackValue = 0.35f;
    float fMixValue = 0.3f;
    float fTone = 6000.0f;
    bool fToneOn = true;
    DelayMode fMode = DelayMode::Stereo;
    float fSpreadMs = 0.0f;
    float fModDepth = 0.0f;
    bool fSnap = true;
};

} // namespace

int delayNoteIndex(float normalizedTime)
{
    // the top of the knob is the longest note, like the unsynced time
    return timeNoteIndex(normalizedTime);
}

const char* delayNoteName(int index)
{
    return noteName(index);
}

double delayNoteBeats(int index)
{
    return noteBeats(index);
}

const BlockDescriptor kDelayBlockDescriptor = {
    "delay",
    "Delay",
    Category::Delay,
    kDelayParamCount,
    {
        ParamSpec { "Time", "ms", kMinDelayMs, kMaxDelayMs, 400.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Feedback", "%", 0.0f, 100.0f, 35.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 500.0f, 20000.0f, 6000.0f, ParamScale::Log },
        ParamSpec { "Mode", "", 0.0f, 2.0f, 1.0f, ParamScale::Choice, kModeNames },
        ParamSpec { "Spread", "ms", 0.0f, 50.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mod", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new DelayBlock()); },
    kDelayParamSync,
    kDelayParamTime,
};

} // namespace namsayin
