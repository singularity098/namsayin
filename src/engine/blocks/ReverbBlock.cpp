// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/ReverbBlock.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

// Dattorro, "Effect Design Part 1" (1997): delay lengths in samples at his 29761 Hz design rate
constexpr double kDesignRate = 29761.0;
constexpr double kInputDiffuserLengths[4] = { 142, 107, 379, 277 };
constexpr double kTankApLengths[2] = { 672, 908 };    // modulated allpasses
constexpr double kTankDelay1Lengths[2] = { 4453, 4217 };
constexpr double kTankAp2Lengths[2] = { 1800, 2656 };
constexpr double kTankDelay2Lengths[2] = { 3720, 3163 };
constexpr double kModExcursion = 16;

// Output taps: {source, position, sign}. Sources: 0/1 = delay 1 of side A/B, 2/3 = allpass 2, 4/5 = delay 2.
struct Tap {
    int source;
    double position;
    float sign;
};
constexpr Tap kLeftTaps[7] = {
    { 1, 266, 1 }, { 1, 2974, 1 }, { 3, 1913, -1 }, { 5, 1996, 1 }, { 0, 1990, -1 }, { 2, 187, -1 }, { 4, 1066, -1 },
};
constexpr Tap kRightTaps[7] = {
    { 0, 353, 1 }, { 0, 3627, 1 }, { 2, 1228, -1 }, { 4, 2673, 1 }, { 1, 2111, -1 }, { 3, 335, -1 }, { 5, 121, -1 },
};

// Early reflections for the room: {ms, gain}, taken from the pre-delay line
struct Reflection {
    double ms;
    float gain;
};
constexpr Reflection kRoomReflections[6] = {
    { 4.3, 0.55f }, { 8.9, 0.45f }, { 13.1, 0.38f }, { 19.7, 0.30f }, { 26.3, 0.22f }, { 35.9, 0.15f },
};

struct Voicing {
    double size;          // scales every tank length
    float inputDiffusion1, inputDiffusion2;
    float decayDiffusion1, decayDiffusion2;
    double modDepth;      // scales the modulation excursion
    double modRateHz;
    float earlyLevel;
};

constexpr Voicing kVoicings[3] = {
    { 0.42, 0.60f, 0.50f, 0.60f, 0.45f, 0.5, 0.9, 0.7f }, // room
    { 1.45, 0.75f, 0.625f, 0.70f, 0.50f, 1.5, 0.5, 0.0f }, // hall
    { 1.00, 0.75f, 0.625f, 0.70f, 0.50f, 1.0, 1.0, 0.0f }, // plate
};
constexpr double kMaxSize = 1.45;

constexpr double kMaxPreDelayMs = 250.0;
constexpr double kMaxReflectionMs = 40.0;

constexpr const char* kTypeNames[] = { "Room", "Hall", "Plate" };

class Allpass {
public:
    void prepare(size_t maxLength) { fLine.prepare(maxLength + 1); }
    void reset() { fLine.reset(); }
    void setLength(size_t length) { fLength = std::max<size_t>(length, 1); }

    float process(float x, float g)
    {
        const float d = fLine.read(fLength);
        const float w = x + g * d;
        fLine.write(w);
        return d - g * w;
    }

    // Fractional length (modulated)
    float process(float x, float g, float length)
    {
        const float d = fLine.readFractional(length);
        const float w = x + g * d;
        fLine.write(w);
        return d - g * w;
    }

    float tap(size_t position) const { return fLine.read(std::max<size_t>(position, 1)); }
    size_t length() const { return fLength; }

private:
    DelayLine fLine;
    size_t fLength = 1;
};

class ReverbBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        const double scale = sampleRate / kDesignRate;
        const double maxTankScale = scale * kMaxSize;

        fPreDelay.prepare(static_cast<size_t>((kMaxPreDelayMs + kMaxReflectionMs) / 1000.0 * sampleRate) + 2);
        for (int i = 0; i < 4; ++i)
            fInputDiffusers[i].prepare(static_cast<size_t>(kInputDiffuserLengths[i] * maxTankScale) + 1);
        for (int side = 0; side < 2; ++side)
        {
            fTankAp1[side].prepare(static_cast<size_t>((kTankApLengths[side] + kModExcursion * 4.0) * maxTankScale) + 4);
            fTankDelay1[side].prepare(static_cast<size_t>(kTankDelay1Lengths[side] * maxTankScale) + 1);
            fTankAp2[side].prepare(static_cast<size_t>(kTankAp2Lengths[side] * maxTankScale) + 1);
            fTankDelay2[side].prepare(static_cast<size_t>(kTankDelay2Lengths[side] * maxTankScale) + 1);
        }

        fMix.prepare(sampleRate);
        fDecayGain.prepare(sampleRate, 0.05);
        fWidthSmoother.prepare(sampleRate);
        applyType();
        updateFilters();
        updateDecay();
        fMix.snap(fMixValue);
        fDecayGain.snap(fDecayGain.target());
        fWidthSmoother.snap(fWidth);
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kReverbParamType: {
            const auto type = static_cast<ReverbType>(std::clamp(static_cast<int>(value), 0, 2));
            if (type != fType)
            {
                fType = type;
                applyType();
                updateDecay();
            }
            break;
        }
        case kReverbParamDecay:
            fDecaySeconds = value;
            updateDecay();
            break;
        case kReverbParamPreDelay:
            fPreDelayMs = value;
            updatePreDelay();
            break;
        case kReverbParamDamping:
            fDamping = value / 100.0f;
            updateDamping();
            break;
        case kReverbParamLowCut:
            fLowCut = value;
            updateFilters();
            break;
        case kReverbParamHighCut:
            fHighCut = value;
            updateFilters();
            break;
        case kReverbParamWidth:
            fWidth = value / 100.0f;
            fWidthSmoother.setTarget(fWidth);
            break;
        case kReverbParamMix:
            fMixValue = value / 100.0f;
            fMix.setTarget(fMixValue);
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        const Voicing& v = kVoicings[static_cast<int>(fType)];
        if (fSnap)
        {
            // first buffer after prepare()/reset(): start at the parameter values instead of gliding to them
            fSnap = false;
            fMix.snap(fMix.target());
            fDecayGain.snap(fDecayGain.target());
            fWidthSmoother.snap(fWidthSmoother.target());
        }

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float inL = io[0][i];
            const float inR = numChannels > 1 ? io[1][i] : inL;
            const float mono = 0.5f * (inL + inR);

            // pre-delay, with the room's early reflections tapped from the same line
            fPreDelay.write(mono);
            float x = fPreDelay.read(fPreDelaySamples);
            float early = 0.0f;
            if (v.earlyLevel > 0.0f)
            {
                for (int r = 0; r < 6; ++r)
                    early += fPreDelay.read(fPreDelaySamples + fReflectionSamples[r]) * kRoomReflections[r].gain;
                early *= v.earlyLevel;
            }

            // input bandwidth and diffusion
            fBandwidthState += (x - fBandwidthState) * 0.75f;
            x = fBandwidthState;
            x = fInputDiffusers[0].process(x, v.inputDiffusion1);
            x = fInputDiffusers[1].process(x, v.inputDiffusion1);
            x = fInputDiffusers[2].process(x, v.inputDiffusion2);
            x = fInputDiffusers[3].process(x, v.inputDiffusion2);

            // tank: two halves feeding each other
            const float decay = fDecayGain.next();
            const float modA = fLfo[0].next() * fModSamples;
            const float modB = fLfo[1].next() * fModSamples;
            float sides[2] = { x + fTankOut[1] * decay, x + fTankOut[0] * decay };
            const float mods[2] = { modA, modB };
            for (int side = 0; side < 2; ++side)
            {
                float s = fTankAp1[side].process(sides[side], -v.decayDiffusion1,
                                                 static_cast<float>(fTankAp1[side].length()) + mods[side]);
                fTankDelay1[side].write(s);
                s = fTankDelay1[side].read(fTankDelay1Lengths[side]);
                fDampState[side] += (s - fDampState[side]) * fDampCoef;
                s = fDampState[side] * decay;
                s = fTankAp2[side].process(s, v.decayDiffusion2);
                fTankDelay2[side].write(s);
                fTankOut[side] = fTankDelay2[side].read(fTankDelay2Lengths[side]);
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (int t = 0; t < 7; ++t)
            {
                wetL += tap(kLeftTaps[t].source, fLeftTapPositions[t]) * kLeftTaps[t].sign;
                wetR += tap(kRightTaps[t].source, fRightTapPositions[t]) * kRightTaps[t].sign;
            }
            wetL = wetL * 0.6f + early;
            wetR = wetR * 0.6f + early;

            if (fLowCutOn)
            {
                wetL = fLowCutFilter[0].process(wetL);
                wetR = fLowCutFilter[1].process(wetR);
            }
            if (fHighCutOn)
            {
                wetL = fHighCutFilter[0].process(wetL);
                wetR = fHighCutFilter[1].process(wetR);
            }

            const float mix = fMix.next();
            const float dryGain = dryGainForMix(mix);
            const float wetGain = wetGainForMix(mix);
            if (numChannels == 1)
            {
                io[0][i] = inL * dryGain + 0.5f * (wetL + wetR) * wetGain;
                continue;
            }
            const float width = fWidthSmoother.next();
            const float mid = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * width;
            io[0][i] = inL * dryGain + (mid + side) * wetGain;
            io[1][i] = inR * dryGain + (mid - side) * wetGain;
        }
    }

    void reset() override
    {
        fPreDelay.reset();
        for (Allpass& ap : fInputDiffusers)
            ap.reset();
        for (int side = 0; side < 2; ++side)
        {
            fTankAp1[side].reset();
            fTankDelay1[side].reset();
            fTankAp2[side].reset();
            fTankDelay2[side].reset();
            fDampState[side] = 0.0f;
            fTankOut[side] = 0.0f;
            fLowCutFilter[side].reset();
            fHighCutFilter[side].reset();
        }
        fBandwidthState = 0.0f;
        fSnap = true;
    }

private:
    float tap(int source, size_t position) const
    {
        switch (source)
        {
        case 0: case 1:
            return fTankDelay1[source].read(position);
        case 2: case 3:
            return fTankAp2[source - 2].tap(position);
        default:
            return fTankDelay2[source - 4].read(position);
        }
    }

    // RT: only recomputes lengths; the buffers are sized for the largest voicing
    void applyType()
    {
        const Voicing& v = kVoicings[static_cast<int>(fType)];
        const double scale = fSampleRate / kDesignRate * v.size;
        auto len = [scale](double samples) { return std::max<size_t>(static_cast<size_t>(samples * scale), 1); };

        for (int i = 0; i < 4; ++i)
            fInputDiffusers[i].setLength(len(kInputDiffuserLengths[i]));
        for (int side = 0; side < 2; ++side)
        {
            fTankAp1[side].setLength(len(kTankApLengths[side] + kModExcursion));
            fTankDelay1Lengths[side] = len(kTankDelay1Lengths[side]);
            fTankAp2[side].setLength(len(kTankAp2Lengths[side]));
            fTankDelay2Lengths[side] = len(kTankDelay2Lengths[side]);
        }
        for (int t = 0; t < 7; ++t)
        {
            fLeftTapPositions[t] = len(kLeftTaps[t].position);
            fRightTapPositions[t] = len(kRightTaps[t].position);
        }
        fModSamples = static_cast<float>(kModExcursion * scale * v.modDepth / v.size);
        fLfo[0].prepare(fSampleRate, v.modRateHz, 0.0);
        fLfo[1].prepare(fSampleRate, v.modRateHz * 1.13, M_PI / 2.0);

        // time around one half of the tank, for the decay gain
        fHalfLoopSeconds = (kTankApLengths[0] + kModExcursion + kTankDelay1Lengths[0] + kTankAp2Lengths[0]
                            + kTankDelay2Lengths[0]) * v.size / kDesignRate;
        for (int r = 0; r < 6; ++r)
            fReflectionSamples[r] = static_cast<size_t>(kRoomReflections[r].ms / 1000.0 * fSampleRate);
        updatePreDelay();
        updateDamping();
    }

    // Each half of the tank applies the decay gain twice (after its first delay and on the way to the other half)
    void updateDecay()
    {
        const double rt60 = std::max(0.05, static_cast<double>(fDecaySeconds));
        const double gain = std::pow(10.0, -3.0 * fHalfLoopSeconds / (2.0 * rt60));
        fDecayGain.setTarget(static_cast<float>(std::min(gain, 0.98)));
    }

    void updatePreDelay()
    {
        fPreDelaySamples = std::max<size_t>(static_cast<size_t>(fPreDelayMs / 1000.0 * fSampleRate), 1);
    }

    // 0% damping leaves the tank bright; 100% rolls it off from ~1.5 kHz
    void updateDamping()
    {
        const double cutoff = 18000.0 * std::pow(1500.0 / 18000.0, static_cast<double>(fDamping));
        const double hz = std::min(cutoff, 0.45 * fSampleRate);
        fDampCoef = static_cast<float>(1.0 - std::exp(-2.0 * M_PI * hz / fSampleRate));
    }

    void updateFilters()
    {
        fLowCutOn = fLowCut > 20.5f;
        fHighCutOn = fHighCut < 19900.0f;
        const double nyquistSafe = 0.45 * fSampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            fLowCutFilter[ch].setHighPass(fSampleRate, std::min<double>(fLowCut, nyquistSafe));
            fHighCutFilter[ch].setLowPass(fSampleRate, std::min<double>(fHighCut, nyquistSafe));
        }
    }

    double fSampleRate = 48000.0;

    DelayLine fPreDelay;
    Allpass fInputDiffusers[4];
    Allpass fTankAp1[2], fTankAp2[2];
    DelayLine fTankDelay1[2], fTankDelay2[2];
    size_t fTankDelay1Lengths[2] {}, fTankDelay2Lengths[2] {};
    size_t fLeftTapPositions[7] {}, fRightTapPositions[7] {};
    size_t fReflectionSamples[6] {};
    size_t fPreDelaySamples = 1;
    float fDampState[2] {}, fTankOut[2] {};
    float fBandwidthState = 0.0f;
    float fDampCoef = 1.0f;
    float fModSamples = 0.0f;
    double fHalfLoopSeconds = 0.36;
    SineLfo fLfo[2];
    Biquad fLowCutFilter[2], fHighCutFilter[2];
    Smoother fMix, fDecayGain, fWidthSmoother;

    ReverbType fType = ReverbType::Hall;
    float fDecaySeconds = 2.5f;
    float fPreDelayMs = 10.0f;
    float fDamping = 0.4f;
    float fLowCut = 80.0f;
    float fHighCut = 9000.0f;
    bool fLowCutOn = true;
    bool fHighCutOn = true;
    float fWidth = 1.0f;
    float fMixValue = 0.25f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kReverbBlockDescriptor = {
    "reverb",
    "Reverb",
    Category::Reverb,
    kReverbParamCount,
    {
        ParamSpec { "Type", "", 0.0f, 2.0f, 1.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Decay", "s", 0.2f, 20.0f, 2.5f, ParamScale::Log },
        ParamSpec { "Pre-Delay", "ms", 0.0f, 250.0f, 10.0f, ParamScale::Linear },
        ParamSpec { "Damping", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Low Cut", "Hz", 20.0f, 1000.0f, 80.0f, ParamScale::Log },
        ParamSpec { "High Cut", "Hz", 1000.0f, 20000.0f, 9000.0f, ParamScale::Log },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 25.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ReverbBlock()); },
};

} // namespace namsayin
