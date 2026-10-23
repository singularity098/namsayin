// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/SynthBlock.hpp"

#include "engine/dsp/Oscillator.hpp"
#include "engine/dsp/PickFilter.hpp"
#include "engine/dsp/PitchTracker.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kWaveNames[] = { "Square", "Saw", "Pulse", "Sub" };
constexpr const char* kOctaveNames[] = { "-2", "-1", "0", "+1" };

constexpr float kEnvOctaves = 5.0f;

class SynthBlock : public Block {
public:
    SynthBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fTracker.prepare(sampleRate);
        fPick.prepare(sampleRate);
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
        case kSynthParamPickFilter:
            fPick.setMs(value);
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
            const bool estimated = fTracker.process(in);
            if (fPick.process(fTracker.sounding(), estimated ? fTracker.hz() : 0.0))
                fTargetHz = fPick.hz();

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
            const float env = fTracker.envelope();
            const bool on = fTracker.sounding() && fPick.open();
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
        fTracker.reset();
        fPick.reset();
        fFilter.reset();
        fTargetHz = fHz = 110.0;
        fPhase = fSubPhase = 0.0;
        fAmp = 0.0f;
        fSnap = true;
    }

private:
    float oscillator(double dt) const
    {
        switch (fWave)
        {
        case SynthWave::Saw:
            return blepSaw(fPhase, dt);
        case SynthWave::Pulse:
            return blepPulse(fPhase, dt, 0.25);
        case SynthWave::Sub:
            return 0.6f * blepSaw(fPhase, dt) + 0.6f * blepPulse(fSubPhase, 0.5 * dt, 0.5);
        default:
            return blepPulse(fPhase, dt, 0.5);
        }
    }

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

    PitchTracker fTracker;
    PickFilter fPick;
    Svf fFilter;
    Smoother fMix;
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
        ParamSpec { "Pick Filter", "ms", 0.0f, 60.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SynthBlock()); },
};

} // namespace namsayin
