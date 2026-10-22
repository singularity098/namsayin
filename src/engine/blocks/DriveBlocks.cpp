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

// ---------------------------------------------------------------------------------------------------------------------
// Envelope drive

class EnvDriveBlock : public DriveBase {
public:
    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        DriveBase::prepare(sampleRate, maxFrames);
        updateEnvelope();
        fAmountSmooth.setLowPass(sampleRate, 30.0);
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kEnvDriveParamDrive:       fDrive.setTarget(value / 100.0f); break;
        case kEnvDriveParamRange:       fRange = value / 100.0f; break;
        case kEnvDriveParamSensitivity: fSensitivity = value / 100.0f; break;
        case kEnvDriveParamRelease:
            fReleaseMs = value;
            updateEnvelope();
            break;
        case kEnvDriveParamTone:  fTone.setTarget(value / 100.0f); break;
        case kEnvDriveParamLevel: fLevel.setTarget(dbToGain(value)); break;
        }
    }

    void reset() override
    {
        DriveBase::reset();
        fEnvelope.reset();
        fAmountSmooth.reset();
        fGain = 1.0f;
        fMakeup = 1.0f;
    }

protected:
    // filters: 0 input high-pass, 1 tone shelf, 2 post low-pass
    void design() override
    {
        for (auto& f : fFilters)
        {
            f[0].setHighPass(fSampleRate, 80.0);
            f[1].setHighShelf(fSampleRate, 1800.0, -12.0 + 18.0 * fTone.value());
            f[2].setLowPass(fSampleRate, std::min(9000.0, 0.45 * fSampleRate));
        }
    }

    float drive(uint32_t ch, float x, float drive) override
    {
        if (ch == 0)
        {
            // the playing level, 0 over a 30 dB window up to the sensitivity point, where it reaches 1
            const float env = fEnvelope.process(x);
            const float envDb = env > 1e-7f ? 20.0f * std::log10(env) : -140.0f;
            const float topDb = -12.0f - 36.0f * fSensitivity;
            const float amount = std::clamp((envDb - (topDb - 30.0f)) / 30.0f, 0.0f, 1.0f);
            const float total = std::clamp(drive + fRange * fAmountSmooth.lowPass(amount), 0.0f, 1.0f);
            fGain = dbToGainD(40.0 * total);
            // make up for what the clipping does to the playing level, so soft notes stay soft and loud ones loud
            // whatever the gain: only the tone changes
            const float e = std::max(env, 1e-3f);
            fMakeup = e / shaper::medium(e * fGain);
        }
        auto& f = fFilters[ch];
        const float clipped = fOversampler[ch].process(f[0].process(x) * fGain, shaper::medium) * fMakeup;
        return f[2].process(f[1].process(clipped));
    }

private:
    void updateEnvelope() { fEnvelope.prepare(fSampleRate, 3.0, fReleaseMs); }

    EnvelopeFollower fEnvelope;
    OnePole fAmountSmooth;
    float fRange = 0.6f;
    float fSensitivity = 0.5f;
    float fReleaseMs = 150.0f;
    float fGain = 1.0f;
    float fMakeup = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Gated fuzz

class GatedFuzzBlock : public DriveBase {
public:
    GatedFuzzBlock() { fDrive.snap(0.7f); }

    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        DriveBase::prepare(sampleRate, maxFrames);
        for (EnvelopeFollower& env : fEnvelope)
            env.prepare(sampleRate, 1.0, 60.0);
        fGateAttack = EnvelopeFollower::coef(sampleRate, 1.0);
        fGateRelease = EnvelopeFollower::coef(sampleRate, 20.0);
        fAliveCoef = EnvelopeFollower::coef(sampleRate, 100.0);
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kGatedFuzzParamDrive: fDrive.setTarget(value / 100.0f); break;
        case kGatedFuzzParamGate:
            fGateThreshold = value <= 0.0f ? 0.0f : dbToGain(-80.0f + 0.6f * value);
            break;
        case kGatedFuzzParamComp: fComp = value / 100.0f; break;
        case kGatedFuzzParamStab:
            fStab = value / 100.0f;
            typeChanged();
            break;
        case kGatedFuzzParamTone:  fTone.setTarget(value / 100.0f); break;
        case kGatedFuzzParamLevel: fLevel.setTarget(dbToGain(value)); break;
        }
    }

    void reset() override
    {
        DriveBase::reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            fEnvelope[ch].reset();
            fBand[ch].reset();
            fLast[ch] = 0.0f;
            fGate[ch] = 0.0f;
            fAlive[ch] = 0.0f;
        }
    }

protected:
    // one-poles: 0 input coupling, 1 tone low-pass, 3 DC blocker; the band-pass picks the squeal's pitch
    void design() override
    {
        // lower Stab: more feedback (it starts to oscillate below about 70%), and higher squeals
        fLoopGain = 12.0f * (1.0f - fStab) * (1.0f - fStab);
        const double squealHz = 200.0 * std::exp2(3.5 * (1.0 - fStab));
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& p = fOnePoles[ch];
            p[0].setLowPass(fSampleRate, 70.0);
            p[1].setLowPass(fSampleRate, 900.0 * std::pow(12.0, fTone.value()));
            p[3].setLowPass(fSampleRate, 15.0);
            fBand[ch].set(fSampleRate, squealHz, 3.0);
        }
    }

    float drive(uint32_t ch, float x, float drive) override
    {
        auto& p = fOnePoles[ch];
        const float env = fEnvelope[ch].process(x);
        const float open = env > fGateThreshold ? 1.0f : 0.0f;
        fGate[ch] = open + (fGate[ch] - open) * (open > fGate[ch] ? fGateAttack : fGateRelease);
        // the feedback only lives while something is playing, so the squeal dies away once you stop
        const float alive = env > 1e-4f ? 1.0f : 0.0f;
        fAlive[ch] = alive + (fAlive[ch] - alive) * fAliveCoef;

        const float gain = dbToGainD(14.0 + 36.0 * drive);
        const float feedback = fLoopGain * fAlive[ch] * fBand[ch].process(fLast[ch]).band * fBand[ch].k();
        const float in = p[0].highPass(x) * gain + feedback;
        // Comp down: a dead zone that chops quiet parts of the wave into a velcro sputter
        const float dead = (1.0f - fComp) * 0.5f;
        const float clipped = fOversampler[ch].process(in, [dead](float v) {
            const float a = std::fabs(v);
            const float gated = a <= dead ? 0.0f : std::copysign(a - dead, v);
            return shaper::asymmetric(gated, 1.0f, 0.5f);
        });
        fLast[ch] = clipped;
        return p[3].highPass(p[1].lowPass(0.22f * clipped * fGate[ch]));
    }

private:
    EnvelopeFollower fEnvelope[2];
    Svf fBand[2];
    float fLast[2] {};
    float fGate[2] {};
    float fAlive[2] {};
    float fGateThreshold = 0.0f;
    float fGateAttack = 0.0f, fGateRelease = 0.0f, fAliveCoef = 0.0f;
    float fComp = 0.7f;
    float fStab = 1.0f;
    float fLoopGain = 0.0f;
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

const BlockDescriptor kEnvDriveBlockDescriptor = {
    "envdrive",
    "Envelope Drive",
    Category::Distortion,
    kEnvDriveParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 20.0f, ParamScale::Linear },
        ParamSpec { "Range", "%", -100.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Sensitivity", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Release", "ms", 20.0f, 1000.0f, 150.0f, ParamScale::Log },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new EnvDriveBlock()); },
};

const BlockDescriptor kGatedFuzzBlockDescriptor = {
    "gatedfuzz",
    "Gated Fuzz",
    Category::Distortion,
    kGatedFuzzParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Gate", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Comp", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Stab", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new GatedFuzzBlock()); },
};

} // namespace namsayin
