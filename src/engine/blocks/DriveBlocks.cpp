// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/DriveBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Oversampler.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

// Tone and type changes redesign filters every kUpdateInterval samples while the tone knob glides
constexpr uint32_t kUpdateInterval = 32;

// What the drive pedals share: two channels of oversamplers, smoothed drive/tone/level/mix, and the loop. A pedal
// implements design() (its filters, for the current type and tone) and drive() (one sample, one channel).
class DriveBase : public Block {
public:
    DriveBase()
    {
        fDrive.snap(0.5f);
        fTone.snap(0.5f);
        fLevel.snap(1.0f);
        fMix.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (Oversampler4x& os : fOversampler)
            os.prepare();
        fDrive.prepare(sampleRate, 0.03);
        fTone.prepare(sampleRate / kUpdateInterval, 0.03);
        fLevel.prepare(sampleRate);
        fMix.prepare(sampleRate);
        fDesign = true;
        reset();
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDrive.snap(fDrive.target());
            fTone.snap(fTone.target());
            fLevel.snap(fLevel.target());
            fMix.snap(fMix.target());
            fDesign = true;
        }
        if (fDesign)
        {
            fDesign = false;
            design();
        }

        for (uint32_t i = 0; i < frames; ++i)
        {
            if (++fCounter >= kUpdateInterval)
            {
                fCounter = 0;
                if (fTone.advance())
                    design();
            }
            const float drive = fDrive.next();
            const float level = fLevel.next();
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                const float y = this->drive(ch, x, drive);
                io[ch][i] = (x + (y - x) * mix) * level;
            }
        }
    }

    void reset() override
    {
        for (Oversampler4x& os : fOversampler)
            os.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            for (Biquad& filter : fFilters[ch])
                filter.reset();
            for (OnePole& filter : fOnePoles[ch])
                filter.reset();
        }
        fSnap = true;
    }

protected:
    virtual void design() = 0;
    virtual float drive(uint32_t ch, float x, float drive) = 0;

    // Changing the type redesigns on the next buffer
    void typeChanged() { fDesign = true; }

    double fSampleRate = 48000.0;
    Oversampler4x fOversampler[2];
    Biquad fFilters[2][4];
    OnePole fOnePoles[2][4];
    Smoother fDrive, fTone, fLevel, fMix;

private:
    uint32_t fCounter = 0;
    bool fDesign = true;
    bool fSnap = true;
};

float dbToGainD(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

// ---------------------------------------------------------------------------------------------------------------------
// Overdrive

constexpr const char* kOdTypeNames[] = { "Screamer", "Blues", "Transparent" };

class OverdriveBlock : public DriveBase {
public:
    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kOdParamDrive: fDrive.setTarget(value / 100.0f); break;
        case kOdParamTone:  fTone.setTarget(value / 100.0f); break;
        case kOdParamLevel: fLevel.setTarget(dbToGain(value)); break;
        case kOdParamMix:   fMix.setTarget(value / 100.0f); break;
        case kOdParamType:
            fType = static_cast<OverdriveType>(std::clamp(static_cast<int>(value), 0, 2));
            typeChanged();
            break;
        }
    }

protected:
    // filters: 0 pre (clip path), 1 pre peak, 2 tone shelf, 3 post low-pass
    void design() override
    {
        const float tone = fTone.value();
        for (auto& f : fFilters)
        {
            switch (fType)
            {
            case OverdriveType::Screamer:
                // the clipping stage only amplifies above ~720 Hz: the mid hump
                f[0].setHighPass(fSampleRate, 720.0, 0.6);
                f[1].setPeak(fSampleRate, 1000.0, 0.0, 1.0);
                f[2].setHighShelf(fSampleRate, 1200.0, -12.0 + 18.0 * tone);
                f[3].setLowPass(fSampleRate, std::min(6500.0, 0.45 * fSampleRate));
                break;
            case OverdriveType::Blues:
                f[0].setHighPass(fSampleRate, 110.0);
                f[1].setPeak(fSampleRate, 700.0, 2.0, 0.7);
                f[2].setHighShelf(fSampleRate, 1500.0, -12.0 + 20.0 * tone);
                f[3].setLowPass(fSampleRate, std::min(8000.0, 0.45 * fSampleRate));
                break;
            case OverdriveType::Transparent:
                f[0].setHighPass(fSampleRate, 150.0);
                f[1].setPeak(fSampleRate, 1000.0, 8.0, 0.7);
                f[2].setHighShelf(fSampleRate, 2000.0, -10.0 + 18.0 * tone);
                f[3].setLowPass(fSampleRate, std::min(10000.0, 0.45 * fSampleRate));
                break;
            }
        }
    }

    float drive(uint32_t ch, float x, float drive) override
    {
        auto& f = fFilters[ch];
        float y;
        switch (fType)
        {
        case OverdriveType::Screamer: {
            const float gain = dbToGainD(6.0 + 36.0 * drive);
            const float hump = f[0].process(x) * gain;
            const float clipped = fOversampler[ch].process(hump, shaper::soft);
            y = 0.5f * x + 0.5f * clipped;
            break;
        }
        case OverdriveType::Blues: {
            const float gain = dbToGainD(3.0 + 33.0 * drive);
            const float in = f[1].process(f[0].process(x)) * gain;
            y = 0.25f * fOversampler[ch].process(in, shaper::medium);
            break;
        }
        default: {
            // the clean blend fades as the drive takes over
            const float gain = dbToGainD(30.0 * drive);
            const float in = f[1].process(f[0].process(x)) * gain;
            const float clipped = 0.4f * fOversampler[ch].process(in, shaper::medium);
            y = x * (1.0f - 0.8f * drive) + clipped * std::sqrt(drive);
            break;
        }
        }
        return f[3].process(f[2].process(y));
    }

private:
    OverdriveType fType = OverdriveType::Screamer;
};

// ---------------------------------------------------------------------------------------------------------------------
// Distortion

constexpr const char* kDistTypeNames[] = { "RAT", "DS-1", "Metal" };

class DistortionBlock : public DriveBase {
public:
    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kDistParamDrive: fDrive.setTarget(value / 100.0f); break;
        case kDistParamTone:  fTone.setTarget(value / 100.0f); break;
        case kDistParamLevel: fLevel.setTarget(dbToGain(value)); break;
        case kDistParamType:
            fType = static_cast<DistortionType>(std::clamp(static_cast<int>(value), 0, 2));
            typeChanged();
            break;
        }
    }

protected:
    // filters: 0 pre high-pass, 1 post (scoop), 2 tone/shelf, 3 post low-pass; one-poles: 0 RAT bandwidth,
    // 1 RAT filter / DS-1 low tone, 2 DS-1 high tone, 3 DC blocker (DS-1's clipping is asymmetric)
    void design() override
    {
        const double tone = fTone.value();
        const double nyquistSafe = 0.45 * fSampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            auto& p = fOnePoles[ch];
            switch (fType)
            {
            case DistortionType::Rat:
                f[0].setHighPass(fSampleRate, 60.0);
                f[3].setLowPass(fSampleRate, std::min(12000.0, nyquistSafe));
                // the Filter knob: fully up is bright (about 16 kHz), fully down about 500 Hz
                p[1].setLowPass(fSampleRate, 500.0 * std::pow(32.0, tone));
                break;
            case DistortionType::Ds1:
                f[0].setHighPass(fSampleRate, 100.0);
                f[3].setLowPass(fSampleRate, std::min(8000.0, nyquistSafe));
                p[1].setLowPass(fSampleRate, 450.0);
                p[2].setLowPass(fSampleRate, 1200.0); // used as a high-pass
                p[3].setLowPass(fSampleRate, 15.0);
                break;
            case DistortionType::Metal:
                f[0].setHighPass(fSampleRate, 150.0, 0.8);
                f[1].setPeak(fSampleRate, 700.0, -8.0, 0.8);
                f[2].setHighShelf(fSampleRate, 2500.0, -10.0 + 18.0 * tone);
                f[3].setLowPass(fSampleRate, std::min(7000.0, nyquistSafe));
                break;
            }
        }
    }

    float drive(uint32_t ch, float x, float drive) override
    {
        auto& f = fFilters[ch];
        auto& p = fOnePoles[ch];
        switch (fType)
        {
        case DistortionType::Rat: {
            const double gainDb = 20.0 + 46.0 * drive;
            // the op-amp's bandwidth shrinks as its gain grows (about 1 MHz gain-bandwidth)
            const double bandwidth = std::min(0.45 * fSampleRate, 1.0e6 / std::pow(10.0, gainDb / 20.0) + 400.0);
            p[0].setLowPass(fSampleRate, bandwidth);
            const float in = p[0].lowPass(f[0].process(x) * dbToGainD(gainDb));
            const float clipped = 0.14f * fOversampler[ch].process(in, shaper::hard);
            return f[3].process(p[1].lowPass(clipped));
        }
        case DistortionType::Ds1: {
            const float in = f[0].process(x) * dbToGainD(20.0 + 30.0 * drive);
            const float clipped = 0.22f * fOversampler[ch].process(in, [](float v) {
                // slightly asymmetric: one diode drop bigger than the other
                return shaper::hard(v + 0.15f) - shaper::hard(0.15f);
            });
            // tone: low-pass and high-pass blended; the middle is scooped
            const float tone = fTone.value();
            const float low = p[1].lowPass(clipped);
            const float high = clipped - p[2].lowPass(clipped);
            return p[3].highPass(f[3].process(1.4f * (low * (1.0f - tone) + high * tone)));
        }
        default: {
            const float g1 = dbToGainD(12.0 + 18.0 * drive);
            const float g2 = dbToGainD(6.0 + 18.0 * drive);
            const float in = f[0].process(x) * g1;
            const float clipped = 0.16f * fOversampler[ch].process(in, [g2](float v) {
                return shaper::hard(g2 * shaper::soft(v));
            });
            return f[3].process(f[2].process(f[1].process(clipped)));
        }
        }
    }

private:
    DistortionType fType = DistortionType::Rat;
};

// ---------------------------------------------------------------------------------------------------------------------
// Fuzz

constexpr const char* kFuzzTypeNames[] = { "Face", "Muff", "Octave" };

class FuzzBlock : public DriveBase {
public:
    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kFuzzParamFuzz:  fDrive.setTarget(value / 100.0f); break;
        case kFuzzParamTone:  fTone.setTarget(value / 100.0f); break;
        case kFuzzParamLevel: fLevel.setTarget(dbToGain(value)); break;
        case kFuzzParamBias:  fBias = value / 100.0f; break;
        case kFuzzParamType:
            fType = static_cast<FuzzType>(std::clamp(static_cast<int>(value), 0, 2));
            typeChanged();
            break;
        }
    }

protected:
    // one-poles: 0 input coupling (high-pass), 1 tone low-pass, 2 Muff high side, 3 DC blocker
    void design() override
    {
        const double tone = fTone.value();
        for (auto& p : fOnePoles)
        {
            p[0].setLowPass(fSampleRate, fType == FuzzType::Muff ? 100.0 : 60.0);
            p[3].setLowPass(fSampleRate, 15.0);
            if (fType == FuzzType::Muff)
            {
                p[1].setLowPass(fSampleRate, 600.0);
                p[2].setLowPass(fSampleRate, 1300.0);
            }
            else
            {
                p[1].setLowPass(fSampleRate, 1200.0 * std::pow(10.0, tone));
            }
        }
    }

    float drive(uint32_t ch, float x, float drive) override
    {
        auto& p = fOnePoles[ch];
        const float in = p[0].highPass(x);
        float y;
        switch (fType)
        {
        case FuzzType::Face: {
            const float gain = dbToGainD(10.0 + 36.0 * drive);
            // below 50%: a dead zone that gates small signals (a starved, sputtery fuzz); above: more asymmetry
            const float dead = std::max(0.0f, 0.5f - fBias) * 0.6f;
            const float lift = std::max(0.0f, fBias - 0.5f) * 0.8f;
            y = 0.2f * fOversampler[ch].process(in * gain, [dead, lift](float v) {
                const float a = std::fabs(v);
                const float gated = a <= dead ? 0.0f : std::copysign(a - dead, v);
                return shaper::asymmetric(gated + lift, 1.0f, 0.45f) - shaper::asymmetric(lift, 1.0f, 0.45f);
            });
            y = p[1].lowPass(y);
            break;
        }
        case FuzzType::Muff: {
            const float g1 = dbToGainD(12.0 + 24.0 * drive);
            const float g2 = dbToGainD(10.0 + 14.0 * drive);
            const float clipped = 0.5f * fOversampler[ch].process(in * g1, [g2](float v) {
                return shaper::soft(g2 * shaper::soft(v));
            });
            // the tone stack: low-pass and high-pass blended, scooped in the middle
            const float tone = fTone.value();
            const float low = p[1].lowPass(clipped);
            const float high = clipped - p[2].lowPass(clipped);
            y = 0.6f * (low * (1.0f - tone) + high * tone);
            break;
        }
        default: {
            const float gain = dbToGainD(24.0 * drive);
            // full-wave rectification of a lightly clipped signal doubles its frequency (rectifying a hard-clipped
            // one would leave almost nothing); then the rectified wave is fuzzed, and a little of the plain signal
            // keeps the fundamental
            y = 0.35f * fOversampler[ch].process(in * 3.0f, [gain](float v) {
                const float s = shaper::soft(v);
                return shaper::soft(gain * 2.0f * (std::fabs(s) - 0.4f)) + 0.1f * s;
            });
            y = p[1].lowPass(y);
            break;
        }
        }
        return p[3].highPass(y);
    }

private:
    FuzzType fType = FuzzType::Face;
    float fBias = 0.5f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Boost

constexpr const char* kBoostTypeNames[] = { "Clean", "Treble", "Mid" };

class BoostBlock : public DriveBase {
public:
    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kBoostParamGain: fDrive.setTarget(dbToGain(value)); break;
        case kBoostParamTone: fTone.setTarget(value / 100.0f); break;
        case kBoostParamType:
            fType = static_cast<BoostType>(std::clamp(static_cast<int>(value), 0, 2));
            typeChanged();
            break;
        }
    }

protected:
    // filters: 0 voicing, 1 tone shelf; one-pole 0: DC blocker (the treble booster clips asymmetrically)
    void design() override
    {
        const double tone = fTone.value();
        for (auto& p : fOnePoles)
            p[0].setLowPass(fSampleRate, 10.0);
        for (auto& f : fFilters)
        {
            switch (fType)
            {
            case BoostType::Clean:
                f[0].setPeak(fSampleRate, 1000.0, 0.0, 1.0);
                break;
            case BoostType::Treble:
                // most of the lows are cut before the gain
                f[0].setLowShelf(fSampleRate, 900.0, -15.0);
                break;
            case BoostType::Mid:
                f[0].setPeak(fSampleRate, 800.0, 10.0, 0.8);
                break;
            }
            f[1].setHighShelf(fSampleRate, 3000.0, -9.0 + 18.0 * tone);
        }
    }

    float drive(uint32_t ch, float x, float gain) override
    {
        auto& f = fFilters[ch];
        const float in = f[0].process(x) * gain;
        // a 9 V pedal's headroom: clean until about 2.0, then rounding off
        const float y = fType == BoostType::Treble
                            ? 3.3f * fOversampler[ch].process(in / 1.5f, [](float v) {
                                  return shaper::asymmetric(v, 1.0f, 0.7f);
                              })
                            : 2.0f * fOversampler[ch].process(in / 2.0f, shaper::hard);
        return f[1].process(fOnePoles[ch][0].highPass(y));
    }

private:
    BoostType fType = BoostType::Clean;
};

} // namespace

const BlockDescriptor kOverdriveBlockDescriptor = {
    "overdrive",
    "Overdrive",
    Category::Distortion,
    kOdParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kOdTypeNames },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new OverdriveBlock()); },
};

const BlockDescriptor kDistortionBlockDescriptor = {
    "distortion",
    "Distortion",
    Category::Distortion,
    kDistParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kDistTypeNames },
    },
    [] { return std::unique_ptr<Block>(new DistortionBlock()); },
};

const BlockDescriptor kFuzzBlockDescriptor = {
    "fuzz",
    "Fuzz",
    Category::Distortion,
    kFuzzParamCount,
    {
        ParamSpec { "Fuzz", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kFuzzTypeNames },
        ParamSpec { "Bias", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new FuzzBlock()); },
};

const BlockDescriptor kBoostBlockDescriptor = {
    "boost",
    "Boost",
    Category::Distortion,
    kBoostParamCount,
    {
        ParamSpec { "Gain", "dB", 0.0f, 24.0f, 6.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kBoostTypeNames },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new BoostBlock()); },
};

} // namespace namsayin
