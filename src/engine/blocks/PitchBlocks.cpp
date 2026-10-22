// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/PitchBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/PitchShifter.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

constexpr double kMaxWindowMs = 120.0;

// What the pitch blocks share: a delay line per channel and N voices per channel reading it, a tone filter per
// channel, the window
template <int N>
class PitchBase : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(std::ceil(kMaxWindowMs / 1000.0 * sampleRate)) + 8);
        updateWindow();
        updateTone();
        reset();
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fLines[ch].reset();
            fTone[ch].reset();
            for (int v = 0; v < N; ++v)
                fVoices[ch][v].reset(0.25 * v); // voices start at different points, so they don't jump together
        }
    }

protected:
    void setWindowMs(float ms)
    {
        fWindowMs = std::min<float>(ms, kMaxWindowMs);
        updateWindow();
    }

    void setToneHz(float hz)
    {
        fToneHz = hz;
        updateTone();
    }

    float tone(uint32_t ch, float x) { return fToneOn ? fTone[ch].process(x) : x; }

    double fSampleRate = 48000.0;
    DelayLine fLines[2];
    PitchVoice fVoices[2][N];

private:
    void updateWindow()
    {
        for (auto& channel : fVoices)
            for (PitchVoice& voice : channel)
                voice.setWindow(fSampleRate, fWindowMs);
    }

    void updateTone()
    {
        fToneOn = fToneHz < 19900.0f;
        for (Biquad& filter : fTone)
            filter.setLowPass(fSampleRate, std::min<double>(fToneHz, 0.45 * fSampleRate));
    }

    float fWindowMs = 45.0f;
    float fToneHz = 20000.0f;
    bool fToneOn = false;
    Biquad fTone[2];
};

// ---------------------------------------------------------------------------------------------------------------------

class PitchShifterBlock : public PitchBase<2> {
public:
    PitchShifterBlock()
    {
        fLevel[0].snap(1.0f);
        fLevel[1].snap(0.0f);
        fDry.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        for (Smoother* s : { &fLevel[0], &fLevel[1], &fDry })
            s->prepare(sampleRate);
        PitchBase::prepare(sampleRate, maxFrames);
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kPitchParamShift1: fShift[0] = std::round(value); break;
        case kPitchParamLevel1: fLevel[0].setTarget(value / 100.0f); break;
        case kPitchParamShift2: fShift[1] = std::round(value); break;
        case kPitchParamLevel2: fLevel[1].setTarget(value / 100.0f); break;
        case kPitchParamDetune: fDetune = value; break;
        case kPitchParamDry:    fDry.setTarget(value / 100.0f); break;
        case kPitchParamWindow: setWindowMs(value); break;
        case kPitchParamTone:   setToneHz(value); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            for (Smoother* s : { &fLevel[0], &fLevel[1], &fDry })
                s->snap(s->target());
        }
        const double ratios[2] = { semitonesToRatio(fShift[0] + fDetune / 100.0),
                                   semitonesToRatio(fShift[1] - fDetune / 100.0) };
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float levels[2] = { fLevel[0].next(), fLevel[1].next() };
            const float dry = fDry.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float wet = 0.0f;
                for (int v = 0; v < 2; ++v)
                {
                    const float y = fVoices[ch][v].read(fLines[ch], ratios[v]); // keep each voice moving
                    wet += y * levels[v];
                }
                io[ch][i] = x * dry + tone(ch, wet);
            }
        }
    }

    void reset() override
    {
        PitchBase::reset();
        fSnap = true;
    }

private:
    float fShift[2] = { 12.0f, 7.0f };
    float fDetune = 0.0f;
    Smoother fLevel[2], fDry;
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kIntervalNames[] = { "+2 Oct", "+1 Oct", "+5th", "+4th", "+2nd", "-2nd",
                                           "-4th",   "-5th",   "-1 Oct", "-2 Oct", "Dive" };
constexpr double kIntervals[] = { 24.0, 12.0, 7.0, 5.0, 2.0, -2.0, -5.0, -7.0, -12.0, -24.0, -36.0 };
constexpr int kIntervalCount = sizeof(kIntervals) / sizeof(kIntervals[0]);
constexpr const char* kWhammyModeNames[] = { "Whammy", "Harmony" };

class WhammyBlock : public PitchBase<1> {
public:
    WhammyBlock() { fPosition.snap(0.0f); }

    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        fPosition.prepare(sampleRate, 0.02);
        PitchBase::prepare(sampleRate, maxFrames);
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kWhammyParamPosition: fPosition.setTarget(value / 100.0f); break;
        case kWhammyParamInterval:
            fInterval = kIntervals[std::clamp(static_cast<int>(value), 0, kIntervalCount - 1)];
            break;
        case kWhammyParamMode:   fHarmony = value >= 0.5f; break;
        case kWhammyParamWindow: setWindowMs(value); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fPosition.snap(fPosition.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double ratio = semitonesToRatio(fInterval * fPosition.next());
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                const float wet = fVoices[ch][0].read(fLines[ch], ratio);
                io[ch][i] = fHarmony ? 0.7071f * (x + wet) : wet;
            }
        }
    }

    void reset() override
    {
        PitchBase::reset();
        fSnap = true;
    }

private:
    double fInterval = 12.0;
    bool fHarmony = false;
    Smoother fPosition;
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr double kPolyRatios[4] = { 0.25, 0.5, 2.0, 4.0 };

class PolyOctaveBlock : public PitchBase<4> {
public:
    PolyOctaveBlock()
    {
        fLevel[0].snap(0.0f);
        fLevel[1].snap(0.7f);
        fLevel[2].snap(0.0f);
        fLevel[3].snap(0.0f);
        fDry.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        for (Smoother& s : fLevel)
            s.prepare(sampleRate);
        fDry.prepare(sampleRate);
        PitchBase::prepare(sampleRate, maxFrames);
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kPolyOctParamSub2:   fLevel[0].setTarget(value / 100.0f); break;
        case kPolyOctParamSub1:   fLevel[1].setTarget(value / 100.0f); break;
        case kPolyOctParamUp1:    fLevel[2].setTarget(value / 100.0f); break;
        case kPolyOctParamUp2:    fLevel[3].setTarget(value / 100.0f); break;
        case kPolyOctParamDry:    fDry.setTarget(value / 100.0f); break;
        case kPolyOctParamTone:   setToneHz(value); break;
        case kPolyOctParamWindow: setWindowMs(value); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            for (Smoother& s : fLevel)
                s.snap(s.target());
            fDry.snap(fDry.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            float levels[4];
            for (int v = 0; v < 4; ++v)
                levels[v] = fLevel[v].next();
            const float dry = fDry.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float wet = 0.0f;
                for (int v = 0; v < 4; ++v)
                {
                    const float y = fVoices[ch][v].read(fLines[ch], kPolyRatios[v]);
                    wet += y * levels[v];
                }
                io[ch][i] = x * dry + tone(ch, wet);
            }
        }
    }

    void reset() override
    {
        PitchBase::reset();
        fSnap = true;
    }

private:
    Smoother fLevel[4], fDry;
    bool fSnap = true;
};

} // namespace

int whammyIntervalCount()
{
    return kIntervalCount;
}

double whammyIntervalSemitones(int index)
{
    return kIntervals[std::clamp(index, 0, kIntervalCount - 1)];
}

const BlockDescriptor kPitchShifterBlockDescriptor = {
    "pitch",
    "Pitch Shifter",
    Category::PitchSynth,
    kPitchParamCount,
    {
        ParamSpec { "Shift 1", "st", -24.0f, 24.0f, 12.0f, ParamScale::Linear },
        ParamSpec { "Level 1", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Shift 2", "st", -24.0f, 24.0f, 7.0f, ParamScale::Linear },
        ParamSpec { "Level 2", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Detune", "ct", 0.0f, 50.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Dry", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Window", "ms", 15.0f, 100.0f, 45.0f, ParamScale::Log },
        ParamSpec { "Tone", "Hz", 1000.0f, 20000.0f, 20000.0f, ParamScale::Log },
    },
    [] { return std::unique_ptr<Block>(new PitchShifterBlock()); },
};

const BlockDescriptor kWhammyBlockDescriptor = {
    "whammy",
    "Whammy",
    Category::PitchSynth,
    kWhammyParamCount,
    {
        ParamSpec { "Position", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Interval", "", 0.0f, static_cast<float>(kIntervalCount - 1), 1.0f, ParamScale::Choice,
                    kIntervalNames },
        ParamSpec { "Mode", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kWhammyModeNames },
        ParamSpec { "Window", "ms", 15.0f, 100.0f, 40.0f, ParamScale::Log },
    },
    [] { return std::unique_ptr<Block>(new WhammyBlock()); },
};

const BlockDescriptor kPolyOctaveBlockDescriptor = {
    "polyoctave",
    "Poly Octave",
    Category::PitchSynth,
    kPolyOctParamCount,
    {
        ParamSpec { "Sub 2", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Sub 1", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Up 1", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Up 2", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Dry", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 500.0f, 20000.0f, 20000.0f, ParamScale::Log },
        ParamSpec { "Window", "ms", 15.0f, 100.0f, 50.0f, ParamScale::Log },
    },
    [] { return std::unique_ptr<Block>(new PolyOctaveBlock()); },
};

} // namespace namsayin
