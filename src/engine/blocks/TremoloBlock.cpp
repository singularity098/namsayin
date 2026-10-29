// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/TremoloBlock.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kTypeNames[] = { "Sine", "Triangle", "Square", "Opto", "Harmonic", "Pan" };

constexpr double kHarmonicCrossoverHz = 750.0;
constexpr double kOptoOnMs = 3.0;
constexpr double kOptoOffMs = 40.0;

class TremoloBlock : public Block {
public:
    TremoloBlock()
    {
        fDepth.snap(0.6f);
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fLfo.prepare(sampleRate);
        fDepth.prepare(sampleRate, 0.03);
        fLevel.prepare(sampleRate);
        for (OnePole& filter : fCrossover)
            filter.setLowPass(sampleRate, kHarmonicCrossoverHz);
        fOptoOn = EnvelopeFollower::coef(sampleRate, kOptoOnMs);
        fOptoOff = EnvelopeFollower::coef(sampleRate, kOptoOffMs);
        updateRate();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kTremParamRate:
            fRateHz = value;
            updateRate();
            break;
        case kTremParamSync:
            fSync = value >= 0.5f;
            updateRate();
            break;
        case kTremParamDepth:
            fDepth.setTarget(value / 100.0f);
            break;
        case kTremParamType:
            fType = static_cast<TremoloType>(std::clamp(static_cast<int>(value), 0, 5));
            break;
        case kTremParamLevel:
            fLevel.setTarget(dbToGain(value));
            break;
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
        // synced: lock the LFO to the song, so the loudest point lands on the beat
        if (fSync)
        {
            const ParamSpec& spec = kTremoloBlockDescriptor.params[kTremParamRate];
            fLfo.syncTo(songPhase(beats, rateNoteBeats(rateNoteIndex(spec.toNormalized(fRateHz)))) + 0.25);
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fDepth.snap(fDepth.target());
            fLevel.snap(fLevel.target());
        }

        for (uint32_t i = 0; i < frames; ++i)
        {
            const double phase = fLfo.advance();
            const float depth = fDepth.next();
            const float level = fLevel.next();

            // 0..1, where 1 is full volume
            float mod;
            switch (fType)
            {
            case TremoloType::Triangle:
                mod = 0.5f + 0.5f * Lfo::shape(LfoShape::Triangle, phase);
                break;
            case TremoloType::Square:
                mod = 0.5f + 0.5f * Lfo::shape(LfoShape::Square, phase);
                break;
            case TremoloType::Opto: {
                // a lamp switched by a square, lighting a photocell: on fast, off slowly
                const float lamp = phase < 0.5 ? 1.0f : 0.0f;
                fOpto = lamp + (fOpto - lamp) * (lamp > fOpto ? fOptoOn : fOptoOff);
                mod = fOpto;
                break;
            }
            default:
                mod = 0.5f + 0.5f * Lfo::shape(LfoShape::Sine, phase);
                break;
            }

            if (fType == TremoloType::Harmonic)
            {
                // lows and highs pulse in opposite phase; a complementary one-pole split, so they sum back flat
                const float lowGain = 1.0f - depth * mod;
                const float highGain = 1.0f - depth * (1.0f - mod);
                for (uint32_t ch = 0; ch < numChannels; ++ch)
                {
                    const float x = io[ch][i];
                    const float low = fCrossover[ch].lowPass(x);
                    io[ch][i] = (low * lowGain + (x - low) * highGain) * level;
                }
            }
            else if (fType == TremoloType::Pan && numChannels == 2)
            {
                // equal-power pan around the centre
                const float pan = depth * (2.0f * mod - 1.0f); // -1..1
                const float angle = static_cast<float>((pan + 1.0f) * M_PI / 4.0);
                const float gainL = std::cos(angle) * static_cast<float>(M_SQRT2);
                const float gainR = std::sin(angle) * static_cast<float>(M_SQRT2);
                io[0][i] *= gainL * level;
                io[1][i] *= gainR * level;
            }
            else
            {
                const float gain = (1.0f - depth * (1.0f - mod)) * level;
                for (uint32_t ch = 0; ch < numChannels; ++ch)
                    io[ch][i] *= gain;
            }
        }
    }

    void reset() override
    {
        fLfo.reset();
        fOpto = 0.0f;
        for (OnePole& filter : fCrossover)
            filter.reset();
        fSnap = true;
    }

private:
    void updateRate()
    {
        const ParamSpec& spec = kTremoloBlockDescriptor.params[kTremParamRate];
        fLfo.setRate(fSync ? rateNoteHz(rateNoteIndex(spec.toNormalized(fRateHz)), fBpm) : fRateHz);
    }

    double fBpm = 120.0;
    float fRateHz = 5.0f;
    bool fSync = false;
    TremoloType fType = TremoloType::Sine;
    Lfo fLfo;
    Smoother fDepth, fLevel;
    OnePole fCrossover[2];
    float fOptoOn = 0.0f, fOptoOff = 0.0f;
    float fOpto = 0.0f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kTremoloBlockDescriptor = {
    "tremolo",
    "Tremolo",
    Category::Modulation,
    kTremParamCount,
    {
        ParamSpec { "Rate", "Hz", 0.5f, 20.0f, 5.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 5.0f, 0.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Level", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new TremoloBlock()); },
    kTremParamSync,
    kTremParamRate,
    true,
};

} // namespace xythos
