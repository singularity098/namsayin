// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/DoppelgangerBlock.hpp"

#include "engine/blocks/DelayBlock.hpp"
#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kOrderNames[] = { "Flange 1st", "Chorus 1st" };
constexpr const char* kVoiceNames[] = { "1 Voice", "2 Voices", "3 Voices" };

constexpr float kMinTimeMs = 1.0f;
constexpr float kMaxTimeMs = 2000.0f;
constexpr double kTimeSmoothingSeconds = 0.12;

constexpr double kFlangeMinMs = 0.25;   // the closest the flanger gets
constexpr double kFlangeSweepMs = 5.0;  // sweep at 100% depth
constexpr double kChorusBaseMs = 10.0;
constexpr double kChorusSweepMs = 8.0;
constexpr int kMaxVoices = 3;

// Keeps runaway feedback bounded (below 1.0): linear up to 0.5, then a smooth tanh knee
float saturate(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.5f)
        return x;
    return std::copysign(0.5f + 0.5f * std::tanh(2.0f * (a - 0.5f)), x);
}

// How the echoes are made, for feedback g and delay T:
//
//   s(t) = x(t) + g² F² s(t - 2T)        one clean line repeating at 2T
//   A(t) = g F s(t - T)                  = g F x(t-T) + g³ F³ x(t-3T) + ...   (odd echoes)
//   B(t) = g² F² s(t - 2T)               = g² F² x(t-2T) + g⁴ F⁴ x(t-4T) + ... (even echoes)
//
// F is the tone filter, once per repeat. The two streams are the echoes of an ordinary delay with feedback g,
// split by parity, so each stream goes through its own effect outside the loop: every echo gets exactly one.
class DoppelgangerBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        const size_t maxDelay = static_cast<size_t>(std::ceil((2.0 * kDelayMaxSeconds + 0.06) * sampleRate));
        const size_t flangeMax =
            static_cast<size_t>(std::ceil((kFlangeMinMs + kFlangeSweepMs + 2.0) / 1000.0 * sampleRate));
        const size_t chorusMax =
            static_cast<size_t>(std::ceil((kChorusBaseMs + kChorusSweepMs + 2.0) / 1000.0 * sampleRate));
        for (Channel& c : fChannels)
        {
            c.line.prepare(maxDelay);
            c.flanger.prepare(flangeMax);
            c.chorus.prepare(chorusMax);
        }
        fTime.prepare(sampleRate, kTimeSmoothingSeconds);
        for (Smoother* s : { &fMix, &fFeedback, &fFlangeDepth, &fFlangeRegen, &fFlangeMix, &fChorusDepth, &fChorusMix,
                             &fBounce })
            s->prepare(sampleRate);
        fFlangeLfo.prepare(sampleRate, fFlangeRate);
        fChorusLfo.prepare(sampleRate, fChorusRate);
        updateOffsets();
        updateFilters();
        updateTargets();
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kDoppelParamTime:
            fTimeMs = value;
            break;
        case kDoppelParamSync:
            fSync = value >= 0.5f;
            break;
        case kDoppelParamFeedback:
            fFeedbackValue = value / 100.0f;
            break;
        case kDoppelParamMix:
            fMixValue = value / 100.0f;
            break;
        case kDoppelParamTone:
            fTone = value;
            updateFilters();
            break;
        case kDoppelParamOrder:
            fChorusFirst = static_cast<int>(value) == static_cast<int>(DoppelgangerOrder::ChorusFirst);
            break;
        case kDoppelParamFlangeRate:
            fFlangeRate = value;
            fFlangeLfo.setRate(fSampleRate, value);
            break;
        case kDoppelParamFlangeDepth:
            fFlangeDepthValue = value / 100.0f;
            break;
        case kDoppelParamFlangeRegen:
            fFlangeRegenValue = value / 100.0f;
            break;
        case kDoppelParamFlangeMix:
            fFlangeMixValue = value / 100.0f;
            break;
        case kDoppelParamChorusRate:
            fChorusRate = value;
            fChorusLfo.setRate(fSampleRate, value);
            break;
        case kDoppelParamChorusDepth:
            fChorusDepthValue = value / 100.0f;
            break;
        case kDoppelParamChorusVoices:
            fVoices = std::clamp(static_cast<int>(value), 1, kMaxVoices);
            updateOffsets();
            break;
        case kDoppelParamChorusMix:
            fChorusMixValue = value / 100.0f;
            break;
        case kDoppelParamWidth:
            fWidth = value / 100.0f;
            updateOffsets();
            break;
        case kDoppelParamBounce:
            fBounceValue = value / 100.0f;
            break;
        }
        updateTargets();
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateTargets();
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            // first buffer after prepare()/reset(): start at the parameter values instead of gliding to them
            fSnap = false;
            for (Smoother* s : { &fTime, &fMix, &fFeedback, &fFlangeDepth, &fFlangeRegen, &fFlangeMix, &fChorusDepth,
                                 &fChorusMix, &fBounce })
                s->snap(s->target());
        }

        const float maxDelay = static_cast<float>(fChannels[0].line.capacity() - 4);
        const float flangeMin = static_cast<float>(kFlangeMinMs / 1000.0 * fSampleRate);
        const float flangeSweep = static_cast<float>(kFlangeSweepMs / 1000.0 * fSampleRate);
        const float chorusBase = static_cast<float>(kChorusBaseMs / 1000.0 * fSampleRate);
        const float chorusSweep = static_cast<float>(kChorusSweepMs / 1000.0 * fSampleRate);
        const float voiceGain = 1.0f / static_cast<float>(fVoices);
        const uint32_t channels = std::min<uint32_t>(numChannels, 2);

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mix = fMix.next();
            const float g = fFeedback.next();
            const float dryGain = dryGainForMix(mix);
            const float wetGain = wetGainForMix(mix);
            const float time = std::clamp(fTime.next(), 1.0f, maxDelay * 0.5f);
            const float flangeDepth = fFlangeDepth.next();
            const float regen = fFlangeRegen.next();
            const float flangeMix = fFlangeMix.next();
            const float chorusDepth = fChorusDepth.next();
            const float chorusMix = fChorusMix.next();
            const float bounce = channels == 2 ? fBounce.next() : 0.0f;
            fFlangeLfo.next();
            fChorusLfo.next();

            for (uint32_t ch = 0; ch < channels; ++ch)
            {
                Channel& c = fChannels[ch];
                const float x = io[ch][i];

                // the two echo streams
                const float odd = g * filter(c, 0, c.line.readFractional(time));
                const float even = saturate(g * g * filter(c, 2, filter(c, 1, c.line.readFractional(2.0f * time))));
                c.line.write(x + even);

                // flanger
                const float flangeIn = fChorusFirst ? even : odd;
                const float flangeLfo = fFlangeLfo.shifted(fFlangeOffset[ch][0], fFlangeOffset[ch][1]);
                const float flangeDelay = flangeMin + (1.0f + flangeLfo) * 0.5f * flangeDepth * flangeSweep;
                const float flangeTap = c.flanger.readFractional(std::max(flangeDelay, 1.0f));
                c.flanger.write(flangeIn + saturate(regen * flangeTap));
                // regen boosts the comb's peaks; pull the level back so more regen isn't much louder
                const float flanged = 0.5f * (flangeIn + flangeTap) * (1.0f - 0.45f * std::fabs(regen));
                const float flangeOut = flangeIn + flangeMix * (flanged - flangeIn);

                // chorus
                const float chorusIn = fChorusFirst ? odd : even;
                float voices = 0.0f;
                for (int v = 0; v < fVoices; ++v)
                {
                    const float lfo = fChorusLfo.shifted(fChorusOffset[ch][v][0], fChorusOffset[ch][v][1]);
                    voices += c.chorus.readFractional(chorusBase + (1.0f + lfo) * 0.5f * chorusDepth * chorusSweep);
                }
                c.chorus.write(chorusIn);
                const float chorused = 0.7f * (chorusIn + voices * voiceGain);
                const float chorusOut = chorusIn + chorusMix * (chorused - chorusIn);

                // Bounce: flanged echoes lean left and chorused ones right
                const float flangeGain = ch == 0 ? 1.0f : 1.0f - bounce;
                const float chorusGain = ch == 0 ? 1.0f - bounce : 1.0f;
                const float wet = flangeOut * flangeGain + chorusOut * chorusGain;
                io[ch][i] = x * dryGain + wet * wetGain;
            }
        }
    }

    void reset() override
    {
        for (Channel& c : fChannels)
        {
            c.line.reset();
            c.flanger.reset();
            c.chorus.reset();
            for (int k = 0; k < 3; ++k)
            {
                c.tone[k].reset();
                c.lowCut[k].reset();
            }
        }
        fSnap = true;
    }

private:
    struct Channel {
        DelayLine line, flanger, chorus;
        // one tone filter per repeat: [0] on the odd stream, [1] and [2] in the loop
        Biquad tone[3], lowCut[3];
    };

    float filter(Channel& c, int k, float x) const
    {
        x = c.lowCut[k].process(x);
        return fToneOn ? c.tone[k].process(x) : x;
    }

    void updateFilters()
    {
        fToneOn = fTone < 19900.0f;
        const double nyquistSafe = 0.45 * fSampleRate;
        for (Channel& c : fChannels)
        {
            for (int k = 0; k < 3; ++k)
            {
                c.tone[k].setLowPass(fSampleRate, std::min<double>(fTone, nyquistSafe));
                c.lowCut[k].setHighPass(fSampleRate, 40.0);
            }
        }
    }

    // LFO phase offsets: the right side is shifted by Width (up to 180° for the flanger, 90° for the chorus);
    // chorus voices are spread evenly around the cycle
    void updateOffsets()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const double side = ch == 1 ? fWidth : 0.0;
            const double flange = side * M_PI;
            fFlangeOffset[ch][0] = static_cast<float>(std::cos(flange));
            fFlangeOffset[ch][1] = static_cast<float>(std::sin(flange));
            for (int v = 0; v < kMaxVoices; ++v)
            {
                const double phase = 2.0 * M_PI * v / fVoices + side * M_PI / 2.0;
                fChorusOffset[ch][v][0] = static_cast<float>(std::cos(phase));
                fChorusOffset[ch][v][1] = static_cast<float>(std::sin(phase));
            }
        }
    }

    void updateTargets()
    {
        double ms = fTimeMs;
        if (fSync)
        {
            const ParamSpec& spec = kDoppelgangerBlockDescriptor.params[kDoppelParamTime];
            ms = delayNoteBeats(delayNoteIndex(spec.toNormalized(fTimeMs))) * 60000.0 / fBpm;
        }
        fTime.setTarget(static_cast<float>(std::min(ms, kDelayMaxSeconds * 1000.0) / 1000.0 * fSampleRate));
        fMix.setTarget(fMixValue);
        fFeedback.setTarget(fFeedbackValue);
        fFlangeDepth.setTarget(fFlangeDepthValue);
        fFlangeRegen.setTarget(fFlangeRegenValue);
        fFlangeMix.setTarget(fFlangeMixValue);
        fChorusDepth.setTarget(fChorusDepthValue);
        fChorusMix.setTarget(fChorusMixValue);
        fBounce.setTarget(fBounceValue);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    Channel fChannels[2];
    Smoother fTime, fMix, fFeedback, fFlangeDepth, fFlangeRegen, fFlangeMix, fChorusDepth, fChorusMix, fBounce;
    SineLfo fFlangeLfo, fChorusLfo;
    float fFlangeOffset[2][2] = { { 1.0f, 0.0f }, { 1.0f, 0.0f } };
    float fChorusOffset[2][kMaxVoices][2] {};

    float fTimeMs = 450.0f;
    bool fSync = false;
    float fFeedbackValue = 0.45f;
    float fMixValue = 0.35f;
    float fTone = 7000.0f;
    bool fToneOn = true;
    bool fChorusFirst = false;
    float fFlangeRate = 0.25f;
    float fFlangeDepthValue = 0.7f;
    float fFlangeRegenValue = 0.7f;
    float fFlangeMixValue = 1.0f;
    float fChorusRate = 0.8f;
    float fChorusDepthValue = 0.6f;
    int fVoices = 3;
    float fChorusMixValue = 1.0f;
    float fWidth = 0.5f;
    float fBounceValue = 0.0f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kDoppelgangerBlockDescriptor = {
    "doppelganger",
    "Doppelganger Delay",
    Category::Delay,
    kDoppelParamCount,
    {
        ParamSpec { "Time", "ms", kMinTimeMs, kMaxTimeMs, 450.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Feedback", "%", 0.0f, 100.0f, 45.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 35.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 500.0f, 20000.0f, 7000.0f, ParamScale::Log },
        ParamSpec { "Order", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kOrderNames },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Bounce", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Flange Rate", "Hz", 0.05f, 5.0f, 0.25f, ParamScale::Log },
        ParamSpec { "Flange Depth", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Flange Regen", "%", -95.0f, 95.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Flange Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Chorus Rate", "Hz", 0.05f, 5.0f, 0.8f, ParamScale::Log },
        ParamSpec { "Chorus Depth", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Voices", "", 1.0f, 3.0f, 3.0f, ParamScale::Choice, kVoiceNames },
        ParamSpec { "Chorus Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new DoppelgangerBlock()); },
};

} // namespace namsayin
