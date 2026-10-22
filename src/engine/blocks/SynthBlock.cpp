// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/SynthBlock.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kWaveNames[] = { "Square", "Saw", "Pulse", "Sub" };
constexpr const char* kOctaveNames[] = { "-2", "-1", "0", "+1" };

constexpr double kMinHz = 40.0;
constexpr double kMaxHz = 1500.0;
constexpr float kHysteresis = 0.3f; // of the envelope
constexpr float kGateLevel = 0.003f;
constexpr float kEnvOctaves = 5.0f;

// PolyBLEP: subtracts the step's aliasing around each discontinuity of a naive saw or square
double polyBlep(double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

class SynthBlock : public Block {
public:
    SynthBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fInputHighPass.setHighPass(sampleRate, 60.0);
        fEnvelope.prepare(sampleRate, 1.0, 80.0);
        updateAmp();
        updateGlide();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kSynthParamWave:
            fWave = static_cast<SynthWave>(std::clamp(static_cast<int>(value), 0, 3));
            break;
        case kSynthParamOctave:
            fOctaveRatio = std::exp2(std::clamp(static_cast<int>(value), 0, 3) - 2);
            break;
        case kSynthParamCutoff:    fCutoff = value; break;
        case kSynthParamResonance: fQ = value; break;
        case kSynthParamEnv:       fEnvAmount = value / 100.0f; break;
        case kSynthParamAttack:
            fAttackMs = value;
            updateAmp();
            break;
        case kSynthParamGlide:
            fGlideMs = value;
            updateGlide();
            break;
        case kSynthParamMix:
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
        for (uint32_t i = 0; i < frames; ++i)
        {
            float in = io[0][i];
            if (numChannels > 1)
                in = 0.5f * (in + io[1][i]);
            track(in);

            // the oscillator glides to the tracked pitch
            fHz += (fTargetHz - fHz) * fGlideCoef;
            const double hz = std::min(fHz * fOctaveRatio, 0.45 * fSampleRate);
            const double dt = hz / fSampleRate;
            fPhase += dt;
            if (fPhase >= 1.0)
                fPhase -= 1.0;
            fSubPhase += 0.5 * dt;
            if (fSubPhase >= 1.0)
                fSubPhase -= 1.0;
            const float osc = oscillator(dt);

            // amplitude: the guitar's envelope, with Attack as a swell at the start of each note
            const float env = fEnvelope.value();
            const bool on = env > kGateLevel;
            const float level = std::min(1.0f, env * 4.0f);
            fAmp += ((on ? level : 0.0f) - fAmp) * (on && level > fAmp ? fAmpAttack : fAmpRelease);

            const double cutoff = fCutoff * std::exp2(fEnvAmount * kEnvOctaves * std::min(1.0f, env * 6.0f));
            fFilter.set(fSampleRate, std::min(cutoff, 0.45 * fSampleRate), fQ);
            const float synth = fFilter.process(osc).low * fAmp * 0.5f;

            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] = io[ch][i] * (1.0f - mix) + synth * mix;
        }
    }

    void reset() override
    {
        fInputHighPass.reset();
        fTrackFilter.reset();
        fEnvelope.reset();
        fFilter.reset();
        fArmed = false;
        fPrevious = 0.0f;
        fSinceCrossing = 0.0;
        fLastPeriod = 0.0;
        fTargetHz = fHz = 110.0;
        fPhase = fSubPhase = 0.0;
        fAmp = 0.0f;
        setTrackFilter(800.0);
        fSnap = true;
    }

private:
    // Zero-crossing pitch tracking: arm on a swing below -hysteresis, trigger on the next upward zero crossing
    // (interpolated between samples); the period is the time between triggers.
    void track(float in)
    {
        const float x = fTrackFilter.process(fInputHighPass.process(in));
        const float env = fEnvelope.process(x);
        fSinceCrossing += 1.0;
        if (x < -kHysteresis * env)
            fArmed = true;
        if (fArmed && fPrevious < 0.0f && x >= 0.0f && env > kGateLevel)
        {
            fArmed = false;
            const double fraction = fPrevious / (fPrevious - x); // where between the samples it crossed
            const double now = fSinceCrossing - 1.0 + fraction;
            const double hz = fSampleRate / now;
            if (hz >= kMinHz && hz <= kMaxHz)
            {
                // a period close to the last one is trusted at once; a jump needs a second period to agree, so a
                // stray crossing from a harmonic doesn't make the synth blip
                if (fLastPeriod > 0.0 && std::fabs(now / fLastPeriod - 1.0) < 0.12)
                {
                    fTargetHz = hz;
                    // keep the tracking filter just above the note, so harmonics can't make extra crossings
                    setTrackFilter(std::clamp(hz * 1.5, 120.0, 1800.0));
                }
                fLastPeriod = now;
            }
            fSinceCrossing = 1.0 - fraction;
        }
        fPrevious = x;
    }

    float oscillator(double dt) const
    {
        switch (fWave)
        {
        case SynthWave::Saw:
            return static_cast<float>(2.0 * fPhase - 1.0 - polyBlep(fPhase, dt));
        case SynthWave::Pulse:
            return square(fPhase, dt, 0.25);
        case SynthWave::Sub:
            return static_cast<float>(0.6 * (2.0 * fPhase - 1.0 - polyBlep(fPhase, dt))
                                      + 0.6 * square(fSubPhase, 0.5 * dt, 0.5));
        default:
            return square(fPhase, dt, 0.5);
        }
    }

    static float square(double phase, double dt, double width)
    {
        double v = phase < width ? 1.0 : -1.0;
        v += polyBlep(phase, dt);
        double falling = phase - width;
        if (falling < 0.0)
            falling += 1.0;
        v -= polyBlep(falling, dt);
        return static_cast<float>(v);
    }

    void setTrackFilter(double hz) { fTrackFilter.setLowPass(fSampleRate, std::min(hz, 0.45 * fSampleRate)); }

    void updateAmp()
    {
        fAmpAttack = 1.0f - EnvelopeFollower::coef(fSampleRate, std::max(1.0f, fAttackMs));
        fAmpRelease = 1.0f - EnvelopeFollower::coef(fSampleRate, 30.0);
    }

    void updateGlide() { fGlideCoef = 1.0 - EnvelopeFollower::coef(fSampleRate, std::max(0.1f, fGlideMs)); }

    double fSampleRate = 48000.0;
    SynthWave fWave = SynthWave::Square;
    double fOctaveRatio = 1.0;
    float fCutoff = 400.0f;
    float fQ = 4.0f;
    float fEnvAmount = 0.6f;
    float fAttackMs = 5.0f;
    float fGlideMs = 10.0f;

    Biquad fInputHighPass, fTrackFilter;
    EnvelopeFollower fEnvelope;
    Svf fFilter;
    Smoother fMix;
    bool fArmed = false;
    float fPrevious = 0.0f;
    double fSinceCrossing = 0.0;
    double fLastPeriod = 0.0;
    double fTargetHz = 110.0, fHz = 110.0;
    double fGlideCoef = 1.0;
    double fPhase = 0.0, fSubPhase = 0.0;
    float fAmp = 0.0f;
    float fAmpAttack = 1.0f, fAmpRelease = 1.0f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kSynthBlockDescriptor = {
    "synth",
    "Synth",
    Category::PitchSynth,
    kSynthParamCount,
    {
        ParamSpec { "Wave", "", 0.0f, 3.0f, 1.0f, ParamScale::Choice, kWaveNames },
        ParamSpec { "Octave", "", 0.0f, 3.0f, 2.0f, ParamScale::Choice, kOctaveNames },
        ParamSpec { "Cutoff", "Hz", 100.0f, 8000.0f, 400.0f, ParamScale::Log },
        ParamSpec { "Resonance", "", 0.5f, 15.0f, 4.0f, ParamScale::Log },
        ParamSpec { "Env", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Attack", "ms", 1.0f, 2000.0f, 5.0f, ParamScale::Log },
        ParamSpec { "Glide", "ms", 0.1f, 500.0f, 10.0f, ParamScale::Log },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SynthBlock()); },
};

} // namespace namsayin
