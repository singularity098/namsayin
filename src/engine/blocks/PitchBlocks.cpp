// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/PitchBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/PeriodFinder.hpp"
#include "engine/dsp/PitchShifter.hpp"
#include "engine/dsp/PitchTracker.hpp"
#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

constexpr double kMaxWindowMs = 120.0;

// What the pitch blocks share: a delay line per channel and N voices per channel reading it, a period finder and a
// pitch tracker on the mono input (so windows can be pitch-synchronous), a tone filter per channel, the window
template <int N>
class PitchBase : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(std::ceil(kMaxWindowMs / 1000.0 * sampleRate)) + 8);
        fMaxWindow = static_cast<float>(kMaxWindowMs / 1000.0 * sampleRate);
        fTracker.prepare(sampleRate);
        fFinder.prepare(sampleRate);
        updateWindow();
        updateTone();
        reset();
    }

    void reset() override
    {
        fTracker.reset();
        fFinder.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            fLines[ch].reset();
            fTone[ch].reset();
            for (int v = 0; v < N; ++v)
                fVoices[ch][v].reset(fWindow, 0.25 * v); // voices start at different points, so they don't jump together
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

    // Follows the input (mono) and returns the window for this sample: a whole number of the signal's repetition
    // period when it has a clear one (a note, or a chord's common period), else of the tracked note, else the Window
    // setting
    float track(float in)
    {
        fTracker.process(in);
        fFinder.process(in);
        double period = fFinder.period();
        if (period <= 0.0 && fTracker.sounding() && fTracker.hz() > 0.0)
            period = fSampleRate / fTracker.hz();
        return pitchSyncWindow(fWindow, period, fMaxWindow);
    }

    double trackedHz() const { return fTracker.hz(); }

    float tone(uint32_t ch, float x) { return fToneOn ? fTone[ch].process(x) : x; }

    double fSampleRate = 48000.0;
    DelayLine fLines[2];
    PitchVoice fVoices[2][N];

private:
    void updateWindow() { fWindow = std::min(static_cast<float>(fWindowMs / 1000.0 * fSampleRate), fMaxWindow); }

    void updateTone()
    {
        fToneOn = fToneHz < 19900.0f;
        for (Biquad& filter : fTone)
            filter.setLowPass(fSampleRate, std::min<double>(fToneHz, 0.45 * fSampleRate));
    }

    float fWindowMs = 45.0f;
    float fWindow = 2160.0f;
    float fMaxWindow = 5760.0f;
    float fToneHz = 20000.0f;
    bool fToneOn = false;
    Biquad fTone[2];
    PitchTracker fTracker;
    PeriodFinder fFinder;
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
            const float window = track(numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i]);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float wet = 0.0f;
                for (int v = 0; v < 2; ++v)
                {
                    const float y = fVoices[ch][v].read(fLines[ch], ratios[v], window); // keep each voice moving
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
            const float window = track(numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i]);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                const float wet = fVoices[ch][0].read(fLines[ch], ratio, window);
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
            const float window = track(numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i]);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float wet = 0.0f;
                for (int v = 0; v < 4; ++v)
                {
                    const float y = fVoices[ch][v].read(fLines[ch], kPolyRatios[v], window);
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

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kKeyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr const char* kScaleNames[] = { "Major", "Minor", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Harm. Minor" };
constexpr int kScales[][7] = {
    { 0, 2, 4, 5, 7, 9, 11 }, // major
    { 0, 2, 3, 5, 7, 8, 10 }, // natural minor
    { 0, 2, 3, 5, 7, 9, 10 }, // dorian
    { 0, 2, 4, 5, 7, 9, 10 }, // mixolydian
    { 0, 2, 4, 6, 7, 9, 11 }, // lydian
    { 0, 1, 3, 5, 7, 8, 10 }, // phrygian
    { 0, 2, 3, 5, 7, 8, 11 }, // harmonic minor
};
constexpr int kScaleCount = sizeof(kScales) / sizeof(kScales[0]);
constexpr const char* kVoiceNames[] = { "-Oct", "-7th", "-6th", "-5th", "-4th", "-3rd", "-2nd", "Unison",
                                        "+2nd", "+3rd", "+4th", "+5th", "+6th", "+7th", "+Oct" };
constexpr int kVoiceCount = 15; // steps -7 .. +7

class HarmonizerBlock : public PitchBase<2> {
public:
    HarmonizerBlock()
    {
        fLevel[0].snap(1.0f);
        fLevel[1].snap(0.0f);
        fDry.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        for (Smoother* s : { &fLevel[0], &fLevel[1], &fDry })
            s->prepare(sampleRate);
        for (Smoother& s : fShift)
            s.prepare(sampleRate, 0.015);
        PitchBase::prepare(sampleRate, maxFrames);
        fSnap = true;
    }

    void setParam(int index, float value) override
    {
        const int choice = static_cast<int>(value);
        switch (index)
        {
        case kHarmParamKey:    fKey = std::clamp(choice, 0, 11); break;
        case kHarmParamScale:  fScale = std::clamp(choice, 0, kScaleCount - 1); break;
        case kHarmParamVoice1: fSteps[0] = std::clamp(choice, 0, kVoiceCount - 1) - 7; break;
        case kHarmParamLevel1: fLevel[0].setTarget(value / 100.0f); break;
        case kHarmParamVoice2: fSteps[1] = std::clamp(choice, 0, kVoiceCount - 1) - 7; break;
        case kHarmParamLevel2: fLevel[1].setTarget(value / 100.0f); break;
        case kHarmParamDry:    fDry.setTarget(value / 100.0f); break;
        case kHarmParamWindow: setWindowMs(value); break;
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
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float window = track(numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i]);
            if (trackedHz() > 0.0)
            {
                // the played note, to the nearest semitone; the harmony is relative to it, so bends carry over
                const int note = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(trackedHz() / 440.0)));
                for (int v = 0; v < 2; ++v)
                    fShift[v].setTarget(static_cast<float>(harmonySemitones(note, fKey, fScale, fSteps[v])));
            }
            const double ratios[2] = { semitonesToRatio(fShift[0].next()), semitonesToRatio(fShift[1].next()) };
            const float levels[2] = { fLevel[0].next(), fLevel[1].next() };
            const float dry = fDry.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                fLines[ch].write(x);
                float wet = 0.0f;
                for (int v = 0; v < 2; ++v)
                    wet += fVoices[ch][v].read(fLines[ch], ratios[v], window) * levels[v];
                io[ch][i] = x * dry + wet;
            }
        }
    }

    void reset() override
    {
        PitchBase::reset();
        fSnap = true;
    }

private:
    int fKey = 0;
    int fScale = 0;
    int fSteps[2] = { 2, 4 };
    Smoother fShift[2];
    Smoother fLevel[2], fDry;
    bool fSnap = true;
};

} // namespace

int harmonySemitones(int note, int key, int scale, int steps)
{
    const int* degrees = kScales[std::clamp(scale, 0, kScaleCount - 1)];
    const int pc = ((note - key) % 12 + 12) % 12;
    // the scale degree at or just below the note
    int degree = 0;
    for (int d = 0; d < 7; ++d)
        if (degrees[d] <= pc)
            degree = d;
    const int target = degree + steps;
    const int octaves = target >= 0 ? target / 7 : -((-target + 6) / 7);
    const int wrapped = target - octaves * 7;
    return degrees[wrapped] + 12 * octaves - degrees[degree];
}

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

namespace namsayin {

const BlockDescriptor kHarmonizerBlockDescriptor = {
    "harmonizer",
    "Harmonizer",
    Category::PitchSynth,
    kHarmParamCount,
    {
        ParamSpec { "Key", "", 0.0f, 11.0f, 0.0f, ParamScale::Choice, kKeyNames },
        ParamSpec { "Scale", "", 0.0f, static_cast<float>(kScaleCount - 1), 0.0f, ParamScale::Choice, kScaleNames },
        ParamSpec { "Voice 1", "", 0.0f, static_cast<float>(kVoiceCount - 1), 9.0f, ParamScale::Choice, kVoiceNames },
        ParamSpec { "Level 1", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Voice 2", "", 0.0f, static_cast<float>(kVoiceCount - 1), 11.0f, ParamScale::Choice, kVoiceNames },
        ParamSpec { "Level 2", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Dry", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Window", "ms", 15.0f, 100.0f, 40.0f, ParamScale::Log },
    },
    [] { return std::unique_ptr<Block>(new HarmonizerBlock()); },
};

} // namespace namsayin
