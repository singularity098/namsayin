// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/RotaryBlock.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kSpeedNames[] = { "Slow", "Fast" };

constexpr double kCrossoverHz = 800.0;
constexpr double kDrumRatio = 0.85;       // the drum turns a little slower than the horn
constexpr double kHornRampSeconds = 1.0;  // time constant of the horn's speed change at 100% Ramp
constexpr double kDrumRampSeconds = 3.5;  // the heavy drum takes longer
constexpr float kHornDopplerMs = 0.35f;   // path length swing
constexpr float kDrumDopplerMs = 0.5f;
constexpr float kHornBaseMs = 1.0f;
constexpr float kDrumBaseMs = 1.5f;
constexpr float kHornAm = 0.45f;          // level swing as the horn faces away
constexpr float kDrumAm = 0.25f;

class RotaryBlock : public Block {
public:
    RotaryBlock()
    {
        fBalance.snap(1.0f);
        fMix.snap(1.0f);
        fDrive.snap(0.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        const size_t size = static_cast<size_t>(std::ceil(0.01 * sampleRate));
        fHorn.prepare(size);
        fDrum.prepare(size);
        for (Biquad& filter : fLow)
            filter.setLowPass(sampleRate, kCrossoverHz);
        for (Biquad& filter : fHigh)
            filter.setHighPass(sampleRate, kCrossoverHz);
        fBalance.prepare(sampleRate);
        fMix.prepare(sampleRate);
        fDrive.prepare(sampleRate);
        updateRamp();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kRotaryParamSpeed:   fFast = value >= 0.5f; break;
        case kRotaryParamSlow:    fSlowHz = value; break;
        case kRotaryParamFast:    fFastHz = value; break;
        case kRotaryParamRamp:
            fRamp = value / 100.0f;
            updateRamp();
            break;
        case kRotaryParamDrive:   fDrive.setTarget(value / 100.0f); break;
        case kRotaryParamBalance: fBalance.setTarget(dbToGain(value)); break;
        case kRotaryParamWidth:   fWidth = value / 100.0f; break;
        case kRotaryParamMix:     fMix.setTarget(value / 100.0f); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fBalance.snap(fBalance.target());
            fMix.snap(fMix.target());
            fDrive.snap(fDrive.target());
            fHornHz = fFast ? fFastHz : fSlowHz;
            fDrumHz = fHornHz * kDrumRatio;
        }

        const float msToSamples = static_cast<float>(fSampleRate / 1000.0);
        const double target = fFast ? fFastHz : fSlowHz;
        // the microphones: left at 0, right up to half a turn round
        const double micOffset = 0.5 * fWidth;
        for (uint32_t i = 0; i < frames; ++i)
        {
            fHornHz += (target - fHornHz) * fHornRamp;
            fDrumHz += (target * kDrumRatio - fDrumHz) * fDrumRamp;
            fHornPhase += fHornHz / fSampleRate;
            fHornPhase -= std::floor(fHornPhase);
            // the drum turns the other way
            fDrumPhase -= fDrumHz / fSampleRate;
            fDrumPhase -= std::floor(fDrumPhase);

            const float drive = fDrive.next();
            const float balance = fBalance.next();
            const float mix = fMix.next();

            // the amp in front: mono, as in the real cabinet
            float in = io[0][i];
            if (numChannels > 1)
                in = 0.5f * (in + io[1][i]);
            float amp = in;
            if (drive > 0.0f)
            {
                const float gain = 1.0f + 20.0f * drive * drive;
                amp = shaper::soft(in * gain) / std::sqrt(gain);
            }
            const float low = fLow[1].process(fLow[0].process(amp));
            const float high = fHigh[1].process(fHigh[0].process(amp));
            fHorn.write(high);
            fDrum.write(low);

            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const double mic = ch == 1 ? micOffset : 0.0;
                const float hornAngle = static_cast<float>(2.0 * M_PI * (fHornPhase + mic));
                const float drumAngle = static_cast<float>(2.0 * M_PI * (fDrumPhase + mic));
                const float hornDelay = (kHornBaseMs + kHornDopplerMs * std::sin(hornAngle)) * msToSamples;
                const float drumDelay = (kDrumBaseMs + kDrumDopplerMs * std::sin(drumAngle)) * msToSamples;
                // loudest when facing the microphone (a quarter turn ahead of the fastest approach)
                const float hornGain = 1.0f - kHornAm * (0.5f - 0.5f * std::cos(hornAngle));
                const float drumGain = 1.0f - kDrumAm * (0.5f - 0.5f * std::cos(drumAngle));
                const float wet = fHorn.readFractional(std::max(1.0f, hornDelay)) * hornGain * balance
                                  + fDrum.readFractional(std::max(1.0f, drumDelay)) * drumGain;
                const float x = io[ch][i];
                io[ch][i] = x + (wet - x) * mix;
            }
        }
    }

    void reset() override
    {
        fHorn.reset();
        fDrum.reset();
        for (int k = 0; k < 2; ++k)
        {
            fLow[k].reset();
            fHigh[k].reset();
        }
        fHornPhase = 0.0;
        fDrumPhase = 0.25;
        fSnap = true;
    }

private:
    void updateRamp()
    {
        const double scale = std::max(0.02, static_cast<double>(fRamp));
        fHornRamp = 1.0 - std::exp(-1.0 / (kHornRampSeconds * scale * fSampleRate));
        fDrumRamp = 1.0 - std::exp(-1.0 / (kDrumRampSeconds * scale * fSampleRate));
    }

    double fSampleRate = 48000.0;
    bool fFast = false;
    float fSlowHz = 0.8f;
    float fFastHz = 6.7f;
    float fRamp = 1.0f;
    float fWidth = 0.5f;
    double fHornHz = 0.8, fDrumHz = 0.7;
    double fHornPhase = 0.0, fDrumPhase = 0.25;
    double fHornRamp = 0.0, fDrumRamp = 0.0;
    DelayLine fHorn, fDrum; // both microphones read from these
    Biquad fLow[2], fHigh[2]; // two cascaded Butterworths each: a Linkwitz-Riley crossover (the halves sum flat)
    Smoother fBalance, fMix, fDrive;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kRotaryBlockDescriptor = {
    "rotary",
    "Rotary",
    Category::Modulation,
    kRotaryParamCount,
    {
        ParamSpec { "Speed", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kSpeedNames },
        ParamSpec { "Slow", "Hz", 0.3f, 2.0f, 0.8f, ParamScale::Log },
        ParamSpec { "Fast", "Hz", 3.0f, 9.0f, 6.7f, ParamScale::Log },
        ParamSpec { "Ramp", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Balance", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new RotaryBlock()); },
};

} // namespace xythos
