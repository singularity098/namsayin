// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/FilterBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xythos {

namespace {

constexpr const char* kTypeNames[] = { "Low-pass", "Band-pass", "High-pass", "Notch" };
constexpr const char* kShapeNames[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Steps" };
constexpr float kDepthOctaves = 3.0f;

class FilterBlock : public Block {
public:
    FilterBlock()
    {
        fDepth.snap(0.5f);
        fMix.snap(1.0f);
        fCutoff.snap(800.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.03);
        fMix.prepare(sampleRate);
        fCutoff.prepare(sampleRate, 0.03);
        // the steps glide for a millisecond, so they don't click
        fStepSmooth.prepare(sampleRate, 0.001);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kFilterParamType:      fType = std::clamp(static_cast<int>(value), 0, 3); break;
        case kFilterParamCutoff:    fCutoff.setTarget(value); break;
        case kFilterParamResonance: fQ = value; break;
        case kFilterParamShape:     fShape = std::clamp(static_cast<int>(value), 0, 5); break;
        case kFilterParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kFilterParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kFilterParamDepth: fDepth.setTarget(value / 100.0f); break;
        case kFilterParamMix:   fMix.setTarget(value / 100.0f); break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateRate();
        }
    }

    void setSongPosition(double beats) override
    {
        // synced: lock the LFO to the song, so each cycle (and each random step) starts on the beat
        if (fSync)
        {
            const ParamSpec& spec = kFilterBlockDescriptor.params[kFilterParamRate];
            fLfo.syncTo(songPhase(beats, rateNoteBeats(rateNoteIndex(spec.toNormalized(fRateHz)))) + 0.0);
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fMix.snap(fMix.target());
            fCutoff.snap(fCutoff.target());
        }
        static constexpr LfoShape shapes[] = { LfoShape::Sine, LfoShape::Triangle, LfoShape::SawUp, LfoShape::SawDown,
                                               LfoShape::Square };
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double previous = fLfo.phase();
            const double phase = fLfo.advance();
            float lfo;
            if (fShape == 5)
            {
                // a new random step each cycle
                if (phase < previous)
                    fStep = random();
                fStepSmooth.setTarget(fStep);
                lfo = fStepSmooth.next();
            }
            else
            {
                lfo = Lfo::shape(shapes[fShape], phase);
            }
            const double freq = fCutoff.next() * std::exp2(fDepth.next() * kDepthOctaves * lfo);
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                fFilter[ch].set(fSampleRate, std::min(freq, 0.45 * fSampleRate), fQ);
                const float x = io[ch][i];
                const Svf::Out out = fFilter[ch].process(x);
                float y;
                switch (fType)
                {
                case 1:  y = out.band * fFilter[ch].k(); break;
                case 2:  y = out.high; break;
                case 3:  y = x - out.band * fFilter[ch].k(); break; // notch: everything but the band
                default: y = out.low; break;
                }
                io[ch][i] = x + (y - x) * mix;
            }
        }
    }

    void reset() override
    {
        fFilter[0].reset();
        fFilter[1].reset();
        fLfo.reset();
        fStep = 0.0f;
        fStepSmooth.snap(0.0f);
        fSnap = true;
    }

private:
    float random()
    {
        // xorshift32, -1..1
        fRandom ^= fRandom << 13;
        fRandom ^= fRandom >> 17;
        fRandom ^= fRandom << 5;
        return static_cast<float>(fRandom) / 2147483648.0f - 1.0f;
    }

    void updateRate()
    {
        const ParamSpec& spec = kFilterBlockDescriptor.params[kFilterParamRate];
        fLfo.setRate(fSync ? rateNoteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    int fType = 0;
    float fQ = 4.0f;
    int fShape = 0;
    float fRateHz = 1.0f;
    bool fSync = false;
    Lfo fLfo;
    Svf fFilter[2];
    Smoother fDepth, fMix, fCutoff, fStepSmooth;
    float fStep = 0.0f;
    uint32_t fRandom = 0x2545f491u;
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kRobotTypeNames[] = { "Low-pass", "Band-pass" };
constexpr const char* kRobotSourceNames[] = { "Random", "Loop 8", "Envelope" };
constexpr int kRobotLoopSteps = 8;

class RobotFilterBlock : public Block {
public:
    RobotFilterBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLfo.prepare(sampleRate);
        fMix.prepare(sampleRate);
        fEnvelope.prepare(sampleRate, 2.0, 100.0);
        // the loop's pattern: fixed, so it sounds the same every time
        uint32_t seed = 0x1b873593u;
        for (float& v : fLoop)
            v = 0.5f + 0.5f * random(seed);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kRobotParamType:      fBandPass = value >= 0.5f; break;
        case kRobotParamSource:    fSource = std::clamp(static_cast<int>(value), 0, 2); break;
        case kRobotParamLow:       fLow = value; break;
        case kRobotParamHigh:      fHigh = value; break;
        case kRobotParamResonance: fQ = value; break;
        case kRobotParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kRobotParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kRobotParamGlide:
            fGlide = value / 100.0f;
            updateRate();
            break;
        case kRobotParamMix: fMix.setTarget(value / 100.0f); break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateRate();
        }
    }

    void setSongPosition(double beats) override
    {
        // synced: each step starts on the beat, and the loop counts from the song's start
        if (fSync)
        {
            const ParamSpec& spec = kRobotFilterBlockDescriptor.params[kRobotParamRate];
            const double cycle = rateNoteBeats(rateNoteIndex(spec.toNormalized(fRateHz)));
            const double phase = songPhase(beats, cycle);
            fLfo.syncTo(phase);
            if (phase > 0.05 && phase < 0.95)
                fStep = static_cast<long long>(std::floor(beats / cycle));
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
            fValue = fHeld;
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            float in = std::fabs(io[0][i]);
            if (numChannels > 1)
                in = std::max(in, std::fabs(io[1][i]));
            const float env = fEnvelope.process(in);

            const double previous = fLfo.phase();
            if (fLfo.advance() < previous)
            {
                ++fStep;
                switch (fSource)
                {
                case 1:
                    fHeld = fLoop[static_cast<size_t>(fStep % kRobotLoopSteps)];
                    break;
                case 2: {
                    // the playing level at the tick, over 40 dB
                    const float db = env > 1e-7f ? 20.0f * std::log10(env) : -140.0f;
                    fHeld = std::clamp((db + 50.0f) / 40.0f, 0.0f, 1.0f);
                    break;
                }
                default:
                    fHeld = 0.5f + 0.5f * random(fRandom);
                    break;
                }
            }
            // the glide runs on the cutoff's octave position, so it sounds even
            fValue = fHeld + (fValue - fHeld) * fGlideCoef;
            const double freq = fLow * std::pow(static_cast<double>(fHigh) / fLow, static_cast<double>(fValue));
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                fFilter[ch].set(fSampleRate, std::min(freq, 0.45 * fSampleRate), fQ);
                const float x = io[ch][i];
                const Svf::Out out = fFilter[ch].process(x);
                const float y = fBandPass ? out.band * fFilter[ch].k() : out.low;
                io[ch][i] = x + (y - x) * mix;
            }
        }
    }

    void reset() override
    {
        fFilter[0].reset();
        fFilter[1].reset();
        fEnvelope.reset();
        fLfo.reset();
        fStep = 0;
        fHeld = fValue = 0.5f;
        fSnap = true;
    }

private:
    // xorshift32, -1..1
    static float random(uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / 2147483648.0f - 1.0f;
    }

    void updateRate()
    {
        const ParamSpec& spec = kRobotFilterBlockDescriptor.params[kRobotParamRate];
        const double hz = fSync ? rateNoteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz;
        fLfo.setRate(hz);
        // Glide 0: a jump smoothed over half a millisecond, so it doesn't click; 100%: most of a step
        const double ms = std::max(0.5, 300.0 * fGlide / hz);
        fGlideCoef = EnvelopeFollower::coef(fSampleRate, ms);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    bool fBandPass = false;
    int fSource = 0;
    float fLow = 300.0f, fHigh = 3000.0f;
    float fQ = 6.0f;
    float fRateHz = 8.0f;
    bool fSync = false;
    float fGlide = 0.1f;
    float fGlideCoef = 0.0f;
    Lfo fLfo;
    Svf fFilter[2];
    EnvelopeFollower fEnvelope;
    Smoother fMix;
    float fLoop[kRobotLoopSteps] {};
    long long fStep = 0;
    float fHeld = 0.5f, fValue = 0.5f;
    uint32_t fRandom = 0x2545f491u;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kFilterBlockDescriptor = {
    "filter",
    "Filter",
    Category::Filter,
    kFilterParamCount,
    {
        ParamSpec { "Type", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Cutoff", "Hz", 60.0f, 8000.0f, 800.0f, ParamScale::Log },
        ParamSpec { "Resonance", "", 0.5f, 15.0f, 4.0f, ParamScale::Log },
        ParamSpec { "Shape", "", 0.0f, 5.0f, 0.0f, ParamScale::Choice, kShapeNames },
        ParamSpec { "Rate", "Hz", 0.05f, 15.0f, 1.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new FilterBlock()); },
    kFilterParamSync,
    kFilterParamRate,
    true,
};

const BlockDescriptor kRobotFilterBlockDescriptor = {
    "robotfilter",
    "Robot Filter",
    Category::Filter,
    kRobotParamCount,
    {
        ParamSpec { "Type", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kRobotTypeNames },
        ParamSpec { "Source", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kRobotSourceNames },
        ParamSpec { "Low", "Hz", 60.0f, 8000.0f, 300.0f, ParamScale::Log },
        ParamSpec { "High", "Hz", 200.0f, 12000.0f, 3000.0f, ParamScale::Log },
        ParamSpec { "Resonance", "", 0.5f, 15.0f, 6.0f, ParamScale::Log },
        ParamSpec { "Rate", "Hz", 0.5f, 30.0f, 8.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Glide", "%", 0.0f, 100.0f, 10.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new RobotFilterBlock()); },
    kRobotParamSync,
    kRobotParamRate,
    true,
};

} // namespace xythos
