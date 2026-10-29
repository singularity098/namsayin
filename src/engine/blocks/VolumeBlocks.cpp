// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/VolumeBlocks.hpp"

#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kTaperNames[] = { "Audio", "Linear" };

class VolumeBlock : public Block {
public:
    VolumeBlock() { fGain.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fGain.prepare(sampleRate, 0.015);
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        if (index == kVolumeParamVolume)
            fTravel = value / 100.0f;
        else if (index == kVolumeParamTaper)
            fLinear = value >= 0.5f;
        fGain.setTarget(fLinear ? fTravel : volumeTaper(fTravel));
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fGain.snap(fGain.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            float gain = fGain.next();
            if (std::fabs(gain - fGain.target()) < 1e-5f)
            {
                // arrive exactly, so heel down is true silence
                fGain.snap(fGain.target());
                gain = fGain.target();
            }
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= gain;
        }
    }

    void reset() override { fSnap = true; }

private:
    float fTravel = 1.0f;
    bool fLinear = false;
    Smoother fGain;
    bool fSnap = true;
};

constexpr float kDbPerLog2 = 6.0205999f;
constexpr double kPauseMs = 40.0;   // this long under the threshold re-arms the swell
constexpr double kFadeMs = 150.0;   // once armed, what's left of the last note fades out over this
constexpr double kDuckMs = 1.5;     // a new note cuts any remainder this fast before swelling (the pick masks it)
constexpr float kRetriggerJump = 2.0f; // a pick attack: the fast envelope jumps above twice the slow one

class SwellBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fFast.prepare(sampleRate, 0.5, 20.0);
        fSlow.prepare(sampleRate, 30.0, 300.0);
        fPauseSamples = static_cast<int>(kPauseMs / 1000.0 * sampleRate);
        fDuckStep = static_cast<float>(1.0 / (kDuckMs / 1000.0 * sampleRate));
        fFadeStep = static_cast<float>(1.0 / (kFadeMs / 1000.0 * sampleRate));
        updateAttack();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kSwellParamAttack:
            fAttackMs = value;
            updateAttack();
            break;
        case kSwellParamThreshold:
            fThreshold = dbToGain(value);
            break;
        case kSwellParamRetrigger:
            fRetrigger = value >= 0.5f;
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            float in = std::fabs(io[0][i]);
            if (numChannels > 1)
                in = std::max(in, std::fabs(io[1][i]));
            const float fast = fFast.process(in);
            const float slow = fSlow.process(in);

            if (fast < fThreshold)
            {
                if (++fQuiet >= fPauseSamples)
                    fState = State::Armed;
            }
            else
            {
                fQuiet = 0;
                if (fState == State::Armed)
                    fState = fGain > 0.0f ? State::Ducking : State::Swelling;
                else if (fRetrigger && fState != State::Ducking && fast > kRetriggerJump * slow
                         && fast > 2.0f * fThreshold && fGain > 0.5f)
                    fState = State::Ducking;
            }

            switch (fState)
            {
            case State::Armed:
                // silent until the next note: fade what's left of the last one out
                fGain = std::max(0.0f, fGain - fFadeStep);
                break;
            case State::Ducking:
                fGain = std::max(0.0f, fGain - fDuckStep);
                if (fGain <= 0.0f)
                    fState = State::Swelling;
                break;
            case State::Swelling:
                fGain = std::min(1.0f, fGain + fAttackStep);
                if (fGain >= 1.0f)
                    fState = State::Open;
                break;
            case State::Open:
                break;
            }

            // an S-curve, like turning a volume knob up smoothly
            const float g = fGain * fGain * (3.0f - 2.0f * fGain);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= g;
        }
    }

    void reset() override
    {
        fFast.reset();
        fSlow.reset();
        fState = State::Armed;
        fGain = 0.0f;
        fQuiet = 0;
    }

private:
    enum class State { Armed, Ducking, Swelling, Open };

    void updateAttack() { fAttackStep = static_cast<float>(1.0 / (std::max(1.0f, fAttackMs) / 1000.0 * fSampleRate)); }

    double fSampleRate = 48000.0;
    float fAttackMs = 400.0f;
    float fThreshold = 0.01f;
    bool fRetrigger = true;
    EnvelopeFollower fFast, fSlow;
    int fPauseSamples = 0;
    float fDuckStep = 0.0f;
    float fFadeStep = 0.0f;
    float fAttackStep = 0.0f;
    State fState = State::Armed;
    float fGain = 0.0f;
    int fQuiet = 0;
};

} // namespace

float volumeTaper(float travel)
{
    // -60 dB to 0 dB along the travel, reaching true silence at the heel
    const float t = std::clamp(travel, 0.0f, 1.0f);
    if (t <= 0.0f)
        return 0.0f;
    const float db = -60.0f * (1.0f - t);
    const float fadeToZero = std::min(1.0f, t * 20.0f); // the last 5% of travel fades the -57..-60 dB out to zero
    return std::exp2(db / kDbPerLog2) * fadeToZero;
}

const BlockDescriptor kVolumeBlockDescriptor = {
    "volume",
    "Volume",
    Category::Dynamics,
    kVolumeParamCount,
    {
        ParamSpec { "Volume", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Taper", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kTaperNames },
    },
    [] { return std::unique_ptr<Block>(new VolumeBlock()); },
};

const BlockDescriptor kSwellBlockDescriptor = {
    "swell",
    "Auto Swell",
    Category::Dynamics,
    kSwellParamCount,
    {
        ParamSpec { "Attack", "ms", 30.0f, 3000.0f, 400.0f, ParamScale::Log },
        ParamSpec { "Threshold", "dB", -70.0f, -10.0f, -45.0f, ParamScale::Linear },
        ParamSpec { "Retrigger", "", 0.0f, 1.0f, 1.0f, ParamScale::Toggle },
    },
    [] { return std::unique_ptr<Block>(new SwellBlock()); },
};

} // namespace xythos
