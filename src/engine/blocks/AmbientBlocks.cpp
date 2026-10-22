// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/AmbientBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Fdn.hpp"
#include "engine/dsp/PitchShifter.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kIntervalNames[] = { "+1 Oct", "+5th", "+Oct+5th", "+2 Oct", "-1 Oct" };
constexpr double kIntervalRatios[] = { 2.0, 1.4983, 2.9966, 4.0, 0.5 };
constexpr double kShimmerWindowMs = 80.0;
constexpr double kMaxSize = 1.5;

class ShimmerBlock : public Block {
public:
    ShimmerBlock()
    {
        fShimmer.snap(0.5f);
        fMix.snap(0.35f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fTank.prepare(sampleRate, kMaxSize);
        fShiftLine.prepare(static_cast<size_t>(std::ceil(kShimmerWindowMs / 1000.0 * sampleRate)) + 8);
        fVoice.setWindow(sampleRate, kShimmerWindowMs);
        fShimmer.prepare(sampleRate, 0.05);
        fMix.prepare(sampleRate);
        fFeedbackHighPass.setLowPass(sampleRate, 150.0);
        applyAll();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kShimmerParamDecay:    fDecay = value; break;
        case kShimmerParamShimmer:  fShimmer.setTarget(value / 100.0f); break;
        case kShimmerParamInterval: fRatio = kIntervalRatios[std::clamp(static_cast<int>(value), 0, 4)]; break;
        case kShimmerParamTone:     fTone = value; break;
        case kShimmerParamSize:     fSize = value / 100.0f; break;
        case kShimmerParamLowCut:   fLowCut = value; break;
        case kShimmerParamMix:      fMix.setTarget(value / 100.0f); break;
        }
        applyAll();
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fShimmer.snap(fShimmer.target());
            fMix.snap(fMix.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float inL = io[0][i];
            const float inR = numChannels > 1 ? io[1][i] : inL;
            const float shimmer = fShimmer.next();
            const float mix = fMix.next();

            // the tail, pitch shifted and fed back in; soft-limited, and scaled so the loop's gain stays under one
            // however long the decay
            fShiftLine.write(fFeedbackHighPass.highPass(0.5f * (fLastL + fLastR)));
            const float shifted = fVoice.read(fShiftLine, fRatio);
            const float feed = shaper::soft(shifted * shimmer * fFeedScale);

            float wetL, wetR;
            fTank.process(inL + feed, inR + feed, wetL, wetR);
            fLastL = wetL;
            fLastR = wetR;
            wetL = fLowCutFilter[0].process(wetL);
            wetR = fLowCutFilter[1].process(wetR);

            const float dryGain = dryGainForMix(mix), wetGain = wetGainForMix(mix);
            if (numChannels == 1)
            {
                io[0][i] = inL * dryGain + 0.5f * (wetL + wetR) * wetGain;
            }
            else
            {
                io[0][i] = inL * dryGain + wetL * wetGain;
                io[1][i] = inR * dryGain + wetR * wetGain;
            }
        }
    }

    void reset() override
    {
        fTank.reset();
        fShiftLine.reset();
        fVoice.reset();
        fFeedbackHighPass.reset();
        fLowCutFilter[0].reset();
        fLowCutFilter[1].reset();
        fLastL = fLastR = 0.0f;
        fSnap = true;
    }

private:
    void applyAll()
    {
        fTank.setSize(0.5 + fSize);
        fTank.setDecay(fDecay);
        fTank.setDamping(fTone);
        for (Biquad& filter : fLowCutFilter)
            filter.setHighPass(fSampleRate, fLowCut);
        // the longer the decay, the more the tank sustains what's fed in: feed back less
        fFeedScale = static_cast<float>(0.9 / std::sqrt(1.0 + fDecay));
    }

    double fSampleRate = 48000.0;
    Fdn fTank;
    DelayLine fShiftLine;
    PitchVoice fVoice;
    OnePole fFeedbackHighPass;
    Biquad fLowCutFilter[2];
    Smoother fShimmer, fMix;
    float fDecay = 6.0f;
    double fRatio = 2.0;
    float fTone = 6000.0f;
    float fSize = 0.6f;
    float fLowCut = 150.0f;
    float fFeedScale = 0.3f;
    float fLastL = 0.0f, fLastR = 0.0f;
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr double kFreezeCaptureDecay = 1.5;  // while not frozen, the tank keeps about this much of the recent sound
constexpr double kLayerDecay = 60.0;         // layering: new playing adds in, and the oldest slowly fades

class FreezeBlock : public Block {
public:
    FreezeBlock() { fLevel.snap(0.7f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fTank.prepare(sampleRate, 1.2);
        fTank.setSize(1.2);
        fTank.setDamping(12000.0);
        fLevel.prepare(sampleRate);
        updateFades();
        updateTone();
        updateTank();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kFreezeParamFreeze:
            fFreeze = value >= 0.5f;
            updateTank();
            break;
        case kFreezeParamLayer:
            fLayer = value >= 0.5f;
            updateTank();
            break;
        case kFreezeParamFadeIn:
            fFadeInMs = value;
            updateFades();
            break;
        case kFreezeParamFadeOut:
            fFadeOutMs = value;
            updateFades();
            break;
        case kFreezeParamTone:
            fTone = value;
            updateTone();
            break;
        case kFreezeParamLevel:
            fLevel.setTarget(value / 100.0f);
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fLevel.snap(fLevel.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float inL = io[0][i];
            const float inR = numChannels > 1 ? io[1][i] : inL;
            // layering feeds new playing in while holding; otherwise the tank only listens while not frozen
            const float feed = fFreeze && !fLayer ? 0.0f : 1.0f;
            float wetL, wetR;
            fTank.process(inL * feed, inR * feed, wetL, wetR);

            if (fFreeze)
                fFade = std::min(1.0f, fFade + fFadeInStep);
            else
                fFade = std::max(0.0f, fFade - fFadeOutStep);
            const float gain = fFade * fFade * fLevel.next() * 2.0f;

            if (fToneOn)
            {
                wetL = fToneFilter[0].process(wetL);
                wetR = fToneFilter[1].process(wetR);
            }
            if (numChannels == 1)
            {
                io[0][i] = inL + 0.5f * (wetL + wetR) * gain;
            }
            else
            {
                io[0][i] = inL + wetL * gain;
                io[1][i] = inR + wetR * gain;
            }
        }
    }

    void reset() override
    {
        fTank.reset();
        fToneFilter[0].reset();
        fToneFilter[1].reset();
        fFade = 0.0f;
        fSnap = true;
    }

private:
    void updateTank()
    {
        if (fFreeze && fLayer)
        {
            fTank.setFrozen(false);
            fTank.setDecay(kLayerDecay);
        }
        else
        {
            fTank.setFrozen(fFreeze);
            fTank.setDecay(kFreezeCaptureDecay);
        }
    }

    void updateFades()
    {
        fFadeInStep = static_cast<float>(1.0 / (std::max(1.0f, fFadeInMs) / 1000.0 * fSampleRate));
        fFadeOutStep = static_cast<float>(1.0 / (std::max(1.0f, fFadeOutMs) / 1000.0 * fSampleRate));
    }

    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        for (Biquad& filter : fToneFilter)
            filter.setLowPass(fSampleRate, std::min<double>(fTone, 0.45 * fSampleRate));
    }

    double fSampleRate = 48000.0;
    Fdn fTank;
    Biquad fToneFilter[2];
    Smoother fLevel;
    bool fFreeze = false;
    bool fLayer = false;
    float fFadeInMs = 300.0f, fFadeOutMs = 1500.0f;
    float fFadeInStep = 0.0f, fFadeOutStep = 0.0f;
    float fTone = 8000.0f;
    bool fToneOn = true;
    float fFade = 0.0f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kShimmerBlockDescriptor = {
    "shimmer",
    "Shimmer",
    Category::Reverb,
    kShimmerParamCount,
    {
        ParamSpec { "Decay", "s", 1.0f, 20.0f, 6.0f, ParamScale::Log },
        ParamSpec { "Shimmer", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Interval", "", 0.0f, 4.0f, 0.0f, ParamScale::Choice, kIntervalNames },
        ParamSpec { "Tone", "Hz", 1000.0f, 16000.0f, 6000.0f, ParamScale::Log },
        ParamSpec { "Size", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Low Cut", "Hz", 20.0f, 1000.0f, 150.0f, ParamScale::Log },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 35.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ShimmerBlock()); },
};

const BlockDescriptor kFreezeBlockDescriptor = {
    "freeze",
    "Freeze",
    Category::Reverb,
    kFreezeParamCount,
    {
        ParamSpec { "Freeze", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Layer", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Fade In", "ms", 10.0f, 3000.0f, 300.0f, ParamScale::Log },
        ParamSpec { "Fade Out", "ms", 50.0f, 10000.0f, 1500.0f, ParamScale::Log },
        ParamSpec { "Tone", "Hz", 1000.0f, 20000.0f, 8000.0f, ParamScale::Log },
        ParamSpec { "Level", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new FreezeBlock()); },
};

} // namespace namsayin
