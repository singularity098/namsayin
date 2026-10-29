// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/ModSweeperBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kEffectNames[] = { "Chorus", "Flanger", "Phaser", "Vibrato" };
constexpr const char* kEffect3Names[] = { "None", "Chorus", "Flanger", "Phaser", "Vibrato" };
constexpr const char* kPatternNames[] = { "Back & Forth", "Cycle" };

constexpr int kEffectCount = 4;
constexpr int kMaxSteps = 4; // Back & Forth with three effects: 1 2 3 2

// The voicings: a two-voice chorus, a classic flanger, a 6-stage phaser and a sine vibrato
constexpr float kChorusCenterMs = 7.0f;
constexpr float kChorusDepthMs = 3.0f;
constexpr float kVibratoCenterMs = 5.0f;
constexpr float kVibratoDepthMs = 2.5f;
constexpr double kChorusMaxMs = 20.0;
constexpr float kFlangerManualMs = 1.5f;
constexpr float kFlangerDepthOctaves = 2.0f;
constexpr double kFlangerMaxMs = 12.0;
constexpr int kPhaserStages = 6;
constexpr float kPhaserCenterHz = 700.0f;
constexpr float kPhaserDepthOctaves = 2.5f;

constexpr double kMinFadeSeconds = 0.015; // the shortest crossfade, so a 0% glide doesn't click
constexpr double kWeightSmoothingSeconds = 0.01;

class ModSweeperBlock : public Block {
public:
    ModSweeperBlock()
    {
        fDepth.snap(0.5f);
        fMix.snap(0.5f);
        fFeedback.snap(0.4f);
        updatePattern();
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            fInput[ch].prepare(static_cast<size_t>(std::ceil(kChorusMaxMs / 1000.0 * sampleRate)) + 4);
            fFlangerLine[ch].prepare(static_cast<size_t>(std::ceil(kFlangerMaxMs / 1000.0 * sampleRate)) + 4);
        }
        for (Lfo& lfo : fLfos)
            lfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.05);
        fMix.prepare(sampleRate);
        fFeedback.prepare(sampleRate);
        for (Smoother& weight : fWeights)
            weight.prepare(sampleRate, kWeightSmoothingSeconds);
        updateSweepRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kSweeperParamEffect1:
        case kSweeperParamEffect2:
            fEffects[index - kSweeperParamEffect1] = std::clamp(static_cast<int>(value), 0, kEffectCount - 1);
            updatePattern();
            break;
        case kSweeperParamEffect3:
            fEffects[2] = std::clamp(static_cast<int>(value), 0, kEffectCount) - 1; // -1: none
            updatePattern();
            break;
        case kSweeperParamPattern:
            fPattern = static_cast<SweeperPattern>(std::clamp(static_cast<int>(value), 0, 1));
            updatePattern();
            break;
        case kSweeperParamSweepRate:
            fSweepHz = value;
            updateSweepRate();
            break;
        case kSweeperParamSync:
            fSync = value >= 0.5f;
            updateSweepRate();
            break;
        case kSweeperParamGlide:
            fGlide = std::clamp(value / 100.0f, 0.0f, 1.0f);
            break;
        case kSweeperParamDepth:
            fDepth.setTarget(value / 100.0f);
            break;
        case kSweeperParamMix:
            fMix.setTarget(value / 100.0f);
            break;
        case kSweeperParamFeedback:
            fFeedback.setTarget(std::clamp(value, -90.0f, 90.0f) / 100.0f);
            break;
        case kSweeperParamWidth:
            fWidth = value / 100.0f;
            break;
        case kSweeperParamChorusRate:
        case kSweeperParamFlangerRate:
        case kSweeperParamPhaserRate:
        case kSweeperParamVibratoRate:
            fLfos[index - kSweeperParamChorusRate].setRate(value);
            break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateSweepRate();
        }
    }

    void setSongPosition(double beats) override
    {
        // synced: count steps from the song's start, so each move to the next effect lands on its note
        if (!fSync)
            return;
        const double steps = beats / stepBeats();
        const double target = steps - std::floor(steps / fSteps) * fSteps;
        double delta = target - fPos;
        delta -= std::round(delta / fSteps) * fSteps;
        if (std::fabs(delta) > 0.001)
            fPos = target;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fMix.snap(fMix.target());
            fFeedback.snap(fFeedback.target());
            float targets[kEffectCount];
            sweepTargets(targets);
            for (int e = 0; e < kEffectCount; ++e)
                fWeights[e].snap(targets[e]);
        }

        const float msToSamples = static_cast<float>(fSampleRate / 1000.0);
        const float flangerMax = static_cast<float>(fFlangerLine[0].capacity() - 4);
        const double piOverRate = M_PI / fSampleRate;
        const double maxFreq = 0.45 * fSampleRate;
        const double sideOffset = 0.5 * fWidth;

        for (uint32_t i = 0; i < frames; ++i)
        {
            fPos += fStep;
            if (fPos >= fSteps)
                fPos -= fSteps;

            float targets[kEffectCount];
            sweepTargets(targets);
            float weight[kEffectCount];
            for (int e = 0; e < kEffectCount; ++e)
            {
                fWeights[e].setTarget(targets[e]);
                weight[e] = fWeights[e].next();
            }

            double phase[kEffectCount];
            for (int e = 0; e < kEffectCount; ++e)
                phase[e] = fLfos[e].advance();
            const float depth = fDepth.next();
            const float mix = fMix.next();
            const float feedback = fFeedback.next();

            // each effect's own mix law, as in its single block
            const float chorusDry = std::cos(mix * static_cast<float>(M_PI / 2.0));
            const float chorusWet = std::sin(mix * static_cast<float>(M_PI / 2.0));
            const float makeup = 1.0f + 1.66f * mix * (1.0f - mix);
            const float flangerWet = mix * (1.0f - 0.5f * std::fabs(feedback)) * makeup;
            const float notchDry = (1.0f - mix) * makeup;

            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const double offset = ch == 1 ? sideOffset : 0.0;
                const float x = io[ch][i];
                fInput[ch].write(x);

                // chorus: two voices in quadrature
                float chorus = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const float lfo = Lfo::shape(LfoShape::Triangle, phase[0] + 0.25 * k + offset);
                    const float delayMs = kChorusCenterMs + depth * kChorusDepthMs * lfo;
                    chorus += fInput[ch].readFractional(std::max(1.0f, delayMs * msToSamples));
                }
                chorus *= static_cast<float>(M_SQRT1_2);

                // flanger, with its feedback kept bounded
                const float flangeLfo = Lfo::shape(LfoShape::Triangle, phase[1] + offset);
                const float flangeDelay = std::clamp(
                    kFlangerManualMs * std::exp2(depth * kFlangerDepthOctaves * flangeLfo) * msToSamples, 1.5f,
                    flangerMax);
                const float flanged = fFlangerLine[ch].readFractional(flangeDelay);
                fFlangerLine[ch].write(std::clamp(x + feedback * flanged, -4.0f, 4.0f));

                // phaser
                const float phaseLfo = Lfo::shape(LfoShape::Triangle, phase[2] + offset);
                const double freq = kPhaserCenterHz * std::exp2(depth * kPhaserDepthOctaves * phaseLfo);
                const float a = coefficient(std::min(freq, maxFreq), piOverRate);
                float phased = x + feedback * fPhaserLast[ch];
                for (int s = 0; s < kPhaserStages; ++s)
                    phased = allpass(ch, s, a, phased);
                fPhaserLast[ch] = std::clamp(phased, -4.0f, 4.0f);

                // vibrato: wet only
                const float vibLfo = Lfo::shape(LfoShape::Sine, phase[3] + offset);
                const float vibrato = fInput[ch].readFractional(
                    std::max(1.0f, (kVibratoCenterMs + depth * kVibratoDepthMs * vibLfo) * msToSamples));

                io[ch][i] = weight[0] * (x * chorusDry + chorus * chorusWet)
                            + weight[1] * (x * notchDry + flanged * flangerWet)
                            + weight[2] * (x * notchDry + phased * mix * makeup)
                            + weight[3] * vibrato;
            }
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fInput[ch].reset();
            fFlangerLine[ch].reset();
            for (float& z : fPhaserState[ch])
                z = 0.0f;
            fPhaserLast[ch] = 0.0f;
        }
        for (Lfo& lfo : fLfos)
            lfo.reset(0.75); // start at the bottom of each sweep
        fPos = 0.0;
        fSnap = true;
    }

private:
    // The weight each effect should have now (they add up to 1)
    void sweepTargets(float* targets) const
    {
        for (int e = 0; e < kEffectCount; ++e)
            targets[e] = 0.0f;
        const int step = std::min(static_cast<int>(fPos), fSteps - 1);
        const double into = fPos - step;
        // hold the effect, then crossfade to the next one over the end of the step
        const double fade = std::min(1.0, std::max(static_cast<double>(fGlide), kMinFadeSeconds * fStepHz));
        double t = (into - (1.0 - fade)) / fade;
        t = t <= 0.0 ? 0.0 : (t >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos(M_PI * t));
        targets[fSequence[step]] += static_cast<float>(1.0 - t);
        targets[fSequence[(step + 1) % fSteps]] += static_cast<float>(t);
    }

    void updatePattern()
    {
        const bool three = fEffects[2] >= 0;
        fSteps = 0;
        fSequence[fSteps++] = fEffects[0];
        fSequence[fSteps++] = fEffects[1];
        if (three)
        {
            fSequence[fSteps++] = fEffects[2];
            if (fPattern == SweeperPattern::BackAndForth)
                fSequence[fSteps++] = fEffects[1];
        }
        fPos -= std::floor(fPos / fSteps) * fSteps;
    }

    double stepBeats() const
    {
        const ParamSpec& spec = kModSweeperBlockDescriptor.params[kSweeperParamSweepRate];
        return rateNoteBeats(rateNoteIndex(spec.toNormalized(fSweepHz)));
    }

    void updateSweepRate()
    {
        fStepHz = fSync ? 1.0 / (stepBeats() * 60.0 / fBpm) : fSweepHz;
        fStep = fStepHz / fSampleRate;
    }

    static float coefficient(double freq, double piOverRate)
    {
        const double t = std::tan(freq * piOverRate);
        return static_cast<float>((t - 1.0) / (t + 1.0));
    }

    float allpass(uint32_t ch, int stage, float a, float x)
    {
        float& z = fPhaserState[ch][stage];
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;

    // the sweep: fPos counts steps (0..fSteps), through fSequence
    int fEffects[3] = { static_cast<int>(SweeperEffect::Chorus), static_cast<int>(SweeperEffect::Flanger), -1 };
    SweeperPattern fPattern = SweeperPattern::BackAndForth;
    int fSequence[kMaxSteps] {};
    int fSteps = 2;
    float fSweepHz = 0.25f;
    bool fSync = false;
    double fStepHz = 0.25;
    double fStep = 0.25 / 48000.0;
    double fPos = 0.0;
    float fGlide = 0.5f;
    Smoother fWeights[kEffectCount];

    float fWidth = 0.5f;
    Lfo fLfos[kEffectCount]; // chorus, flanger, phaser, vibrato
    Smoother fDepth, fMix, fFeedback;
    DelayLine fInput[2];       // chorus and vibrato read the dry input
    DelayLine fFlangerLine[2]; // the flanger's has its feedback in it
    float fPhaserState[2][kPhaserStages] {};
    float fPhaserLast[2] {};
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kModSweeperBlockDescriptor = {
    "modsweeper",
    "Modulation Sweeper",
    Category::Modulation,
    kSweeperParamCount,
    {
        ParamSpec { "Effect 1", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kEffectNames },
        ParamSpec { "Effect 2", "", 0.0f, 3.0f, 1.0f, ParamScale::Choice, kEffectNames },
        ParamSpec { "Effect 3", "", 0.0f, 4.0f, 0.0f, ParamScale::Choice, kEffect3Names },
        ParamSpec { "Pattern", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kPatternNames },
        ParamSpec { "Sweep Rate", "Hz", 0.02f, 4.0f, 0.25f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Glide", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Feedback", "%", -90.0f, 90.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Chorus Rate", "Hz", 0.05f, 10.0f, 0.8f, ParamScale::Log },
        ParamSpec { "Flanger Rate", "Hz", 0.02f, 10.0f, 0.3f, ParamScale::Log },
        ParamSpec { "Phaser Rate", "Hz", 0.02f, 10.0f, 0.5f, ParamScale::Log },
        ParamSpec { "Vibrato Rate", "Hz", 0.5f, 12.0f, 5.0f, ParamScale::Log },
    },
    [] { return std::unique_ptr<Block>(new ModSweeperBlock()); },
    kSweeperParamSync,
    kSweeperParamSweepRate,
    true,
};

} // namespace xythos
