// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/EchoBlocks.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kHeadNames[] = { "1", "2", "3", "1+2", "1+3", "2+3", "1+2+3" };
constexpr int kHeadMasks[] = { 1, 2, 4, 3, 5, 6, 7 };
constexpr int kHeadChoices = 7;

constexpr double kWowHz = 0.7;
constexpr double kFlutterHz = 9.5;
constexpr double kWowDepth = 0.004;     // speed deviation at 100%
constexpr double kFlutterDepth = 0.0012;
constexpr double kDriftDepth = 0.002;

// Keeps runaway feedback bounded (the same knee as the digital delay)
float limitFeedback(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.5f)
        return x;
    return std::copysign(0.5f + 0.5f * std::tanh(2.0f * (a - 0.5f)), x);
}

class TapeEchoBlock : public Block {
public:
    TapeEchoBlock()
    {
        fFeedback.snap(0.4f);
        fMix.snap(0.35f);
        fDrive.snap(0.3f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        // three heads at up to kTapeMaxHeadMs each, plus room for the speed wander
        fLine.prepare(static_cast<size_t>(std::ceil((3.0 * kTapeMaxHeadMs * 1.02 + 10.0) / 1000.0 * sampleRate)));
        fTime.prepare(sampleRate, 0.25);
        fFeedback.prepare(sampleRate);
        fMix.prepare(sampleRate);
        fDrive.prepare(sampleRate);
        fWowLfo.prepare(sampleRate);
        fWowLfo.setRate(kWowHz);
        fFlutterLfo.prepare(sampleRate);
        fFlutterLfo.setRate(kFlutterHz);
        fDrift.prepare(sampleRate, 0.3, 0x51ed2701u);
        fHighPass.setLowPass(sampleRate, 90.0);
        updateTone();
        updateTime();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kTapeParamTime:
            fTimeMs = value;
            updateTime();
            break;
        case kTapeParamSync:
            fSync = value >= 0.5f;
            updateTime();
            break;
        case kTapeParamFeedback: fFeedback.setTarget(value / 100.0f); break;
        case kTapeParamMix:      fMix.setTarget(value / 100.0f); break;
        case kTapeParamTone:
            fTone = value;
            updateTone();
            break;
        case kTapeParamWow:   fWow = value / 100.0f; break;
        case kTapeParamDrive: fDrive.setTarget(value / 100.0f); break;
        case kTapeParamHeads:
            fHeads = kHeadMasks[std::clamp(static_cast<int>(value), 0, kHeadChoices - 1)];
            break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateTime();
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fTime.snap(fTime.target());
            fFeedback.snap(fFeedback.target());
            fMix.snap(fMix.target());
            fDrive.snap(fDrive.target());
        }

        int heads = 0;
        for (int h = 0; h < 3; ++h)
            heads += (fHeads >> h) & 1;
        const float headGain = 1.0f / std::sqrt(static_cast<float>(std::max(1, heads)));
        const float maxDelay = static_cast<float>(fLine.capacity() - 4);

        for (uint32_t i = 0; i < frames; ++i)
        {
            // the tape's speed wanders: slow wow, fast flutter, and a random drift
            const double wander = fWow * (kWowDepth * Lfo::shape(LfoShape::Sine, fWowLfo.advance())
                                          + kFlutterDepth * Lfo::shape(LfoShape::Sine, fFlutterLfo.advance())
                                          + kDriftDepth * fDrift.next());
            const float time = fTime.next() * static_cast<float>(1.0 + wander);
            const float feedback = fFeedback.next();
            const float mix = fMix.next();
            const float drive = fDrive.next();

            float echo = 0.0f;
            for (int h = 0; h < 3; ++h)
                if (fHeads & (1 << h))
                    echo += fLine.readFractional(std::clamp(time * static_cast<float>(h + 1), 1.0f, maxDelay));
            echo *= headGain;
            // each pass loses highs and lows
            echo = fHighPass.highPass(fToneOn ? fToneFilter.process(echo) : echo);

            const float in = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            // the record head saturates: louder playing and more Drive compress the repeats
            const float gain = 1.0f + 6.0f * drive;
            const float record = shaper::soft((in + limitFeedback(echo * feedback)) * gain) / gain;
            fLine.write(record);

            const float dryGain = dryGainForMix(mix), wetGain = wetGainForMix(mix);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] = io[ch][i] * dryGain + echo * wetGain;
        }
    }

    void reset() override
    {
        fLine.reset();
        fToneFilter.reset();
        fHighPass.reset();
        fSnap = true;
    }

private:
    void updateTime()
    {
        double ms = fTimeMs;
        if (fSync)
        {
            const ParamSpec& spec = kTapeEchoBlockDescriptor.params[kTapeParamTime];
            ms = noteBeats(timeNoteIndex(spec.toNormalized(fTimeMs))) * 60000.0 / fBpm;
        }
        fTime.setTarget(static_cast<float>(std::min(ms, kTapeMaxHeadMs) / 1000.0 * fSampleRate));
    }

    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        fToneFilter.setLowPass(fSampleRate, std::min<double>(fTone, 0.45 * fSampleRate));
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fTimeMs = 300.0f;
    bool fSync = false;
    float fTone = 3500.0f;
    bool fToneOn = true;
    float fWow = 0.3f;
    int fHeads = 1;
    DelayLine fLine;
    Biquad fToneFilter;
    OnePole fHighPass;
    Lfo fWowLfo, fFlutterLfo;
    RandomLfo fDrift;
    Smoother fTime, fFeedback, fMix, fDrive;
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr float kDbPerLog2 = 6.0205999f;
constexpr double kDuckMaxMs = 2000.0;

class DuckingDelayBlock : public Block {
public:
    DuckingDelayBlock()
    {
        fFeedback.snap(0.4f);
        fMix.snap(0.35f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(std::ceil((kDuckMaxMs + 10.0) / 1000.0 * sampleRate)));
        fTime.prepare(sampleRate, 0.12);
        fFeedback.prepare(sampleRate);
        fMix.prepare(sampleRate);
        updateTone();
        updateTime();
        updateEnvelope();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kDuckParamTime:
            fTimeMs = value;
            updateTime();
            break;
        case kDuckParamSync:
            fSync = value >= 0.5f;
            updateTime();
            break;
        case kDuckParamFeedback: fFeedback.setTarget(value / 100.0f); break;
        case kDuckParamMix:      fMix.setTarget(value / 100.0f); break;
        case kDuckParamTone:
            fTone = value;
            updateTone();
            break;
        case kDuckParamThreshold: fThresholdDb = value; break;
        case kDuckParamDuck:      fDuckDb = value; break;
        case kDuckParamRelease:
            fReleaseMs = value;
            updateEnvelope();
            break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateTime();
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fTime.snap(fTime.target());
            fFeedback.snap(fFeedback.target());
            fMix.snap(fMix.target());
        }
        const float maxDelay = static_cast<float>(fLines[0].capacity() - 4);
        float maxDuck = 0.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float time = std::clamp(fTime.next(), 1.0f, maxDelay);
            const float feedback = fFeedback.next();
            const float mix = fMix.next();

            float peak = std::fabs(io[0][i]);
            if (numChannels > 1)
                peak = std::max(peak, std::fabs(io[1][i]));
            const float env = fEnvelope.process(peak);
            const float envDb = env > 1e-7f ? kDbPerLog2 * std::log2(env) : -140.0f;
            // full ducking 12 dB over the threshold, eased in from the threshold
            const float duck = fDuckDb * std::clamp((envDb - fThresholdDb) / 12.0f, 0.0f, 1.0f);
            maxDuck = std::max(maxDuck, duck);
            const float duckGain = std::exp2(-duck / kDbPerLog2);

            const float dryGain = dryGainForMix(mix), wetGain = wetGainForMix(mix) * duckGain;
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                const float wet = fLines[ch].readFractional(time);
                float fb = fLowCut[ch].highPass(wet);
                if (fToneOn)
                    fb = fToneFilter[ch].process(fb);
                fLines[ch].write(x + limitFeedback(fb * feedback));
                io[ch][i] = x * dryGain + wet * wetGain;
            }
        }
        fMeter = maxDuck;
    }

    float meter() const override { return fMeter; }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fLines[ch].reset();
            fToneFilter[ch].reset();
            fLowCut[ch].reset();
        }
        fEnvelope.reset();
        fMeter = 0.0f;
        fSnap = true;
    }

private:
    void updateTime()
    {
        double ms = fTimeMs;
        if (fSync)
        {
            const ParamSpec& spec = kDuckingDelayBlockDescriptor.params[kDuckParamTime];
            ms = noteBeats(timeNoteIndex(spec.toNormalized(fTimeMs))) * 60000.0 / fBpm;
        }
        fTime.setTarget(static_cast<float>(std::min(ms, kDuckMaxMs) / 1000.0 * fSampleRate));
    }

    void updateTone()
    {
        fToneOn = fTone < 19900.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            fToneFilter[ch].setLowPass(fSampleRate, std::min<double>(fTone, 0.45 * fSampleRate));
            fLowCut[ch].setLowPass(fSampleRate, 40.0);
        }
    }

    void updateEnvelope() { fEnvelope.prepare(fSampleRate, 5.0, fReleaseMs); }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fTimeMs = 400.0f;
    bool fSync = false;
    float fTone = 6000.0f;
    bool fToneOn = true;
    float fThresholdDb = -30.0f;
    float fDuckDb = 15.0f;
    float fReleaseMs = 300.0f;
    float fMeter = 0.0f;
    DelayLine fLines[2];
    Biquad fToneFilter[2];
    OnePole fLowCut[2];
    EnvelopeFollower fEnvelope;
    Smoother fTime, fFeedback, fMix;
    bool fSnap = true;
};

} // namespace

int tapeHeadsMask(int choice)
{
    return kHeadMasks[std::clamp(choice, 0, kHeadChoices - 1)];
}

const BlockDescriptor kTapeEchoBlockDescriptor = {
    "tapeecho",
    "Tape Echo",
    Category::Delay,
    kTapeParamCount,
    {
        ParamSpec { "Time", "ms", 40.0f, static_cast<float>(kTapeMaxHeadMs), 300.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Feedback", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 35.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 800.0f, 20000.0f, 3500.0f, ParamScale::Log },
        ParamSpec { "Wow", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Heads", "", 0.0f, static_cast<float>(kHeadChoices - 1), 0.0f, ParamScale::Choice, kHeadNames },
    },
    [] { return std::unique_ptr<Block>(new TapeEchoBlock()); },
    kTapeParamSync,
    kTapeParamTime,
    false,
};

const BlockDescriptor kDuckingDelayBlockDescriptor = {
    "duckdelay",
    "Ducking Delay",
    Category::Delay,
    kDuckParamCount,
    {
        ParamSpec { "Time", "ms", 1.0f, static_cast<float>(kDuckMaxMs), 400.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Feedback", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 35.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 500.0f, 20000.0f, 6000.0f, ParamScale::Log },
        ParamSpec { "Threshold", "dB", -60.0f, 0.0f, -30.0f, ParamScale::Linear },
        ParamSpec { "Duck", "dB", 0.0f, 30.0f, 15.0f, ParamScale::Linear },
        ParamSpec { "Release", "ms", 50.0f, 2000.0f, 300.0f, ParamScale::Log },
    },
    [] { return std::unique_ptr<Block>(new DuckingDelayBlock()); },
    kDuckParamSync,
    kDuckParamTime,
    false,
    "Ducking",
    30.0f,
};

} // namespace xythos
