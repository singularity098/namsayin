// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/ResonatorBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Oversampler.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace xythos {

namespace {

constexpr const char* kKeyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr const char* kTuningNames[] = { "Drone", "Major", "Minor", "Sitar", "Open" };
constexpr int kStrings = 12;
// Each tuning's strings, in semitones from the key's note in the third octave (C3..B3); the doubled strings are
// detuned a little (kDetune), so they shimmer against each other as real courses do
constexpr int kTunings[5][kStrings] = {
    { -12, -5, 0, 0, 7, 12, 12, 19, 24, 24, 31, 36 },   // Drone: root and fifth over four octaves
    { -12, -8, -5, 0, 4, 7, 12, 16, 19, 24, 28, 31 },  // Major
    { -12, -9, -5, 0, 3, 7, 12, 15, 19, 24, 27, 31 },  // Minor
    { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19 },      // Sitar: the sympathetic strings tuned to a raga's scale
    { -12, -7, -2, 3, 7, 12, 0, 5, 10, 15, 19, 24 },   // Open: a guitar's open strings (the key is the low string), twice
};
constexpr float kDetune[kStrings] = { 0.0f, 0.0f, -2.0f, 2.5f, 0.0f, -1.5f, 2.0f, 0.0f, -2.0f, 1.5f, 0.0f, 1.0f };
constexpr float kMinHz = 55.0f;

class SympatheticBlock : public Block {
public:
    SympatheticBlock() { fLevel.snap(0.5f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fLevel.prepare(sampleRate);
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(sampleRate / kMinHz) + 8);
        fHighPass.setHighPass(sampleRate, 60.0);
        fUpdate = true;
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kSympParamKey:     fKey = std::clamp(static_cast<int>(value), 0, 11); break;
        case kSympParamTuning:  fTuning = std::clamp(static_cast<int>(value), 0, 4); break;
        case kSympParamDecay:   fDecay = value; break;
        case kSympParamDamping: fDamping = value / 100.0f; break;
        case kSympParamExcite:  fExcite = value / 100.0f; break;
        case kSympParamWidth:   fWidth = value / 100.0f; break;
        case kSympParamLevel:   fLevel.setTarget(value / 100.0f); return;
        }
        fUpdate = true;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fUpdate)
            updateStrings();
        if (fSnap)
        {
            fSnap = false;
            fLevel.snap(fLevel.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mono = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            const float drive = fHighPass.process(mono) * fInputGain;
            float left = 0.0f, right = 0.0f;
            for (int s = 0; s < kStrings; ++s)
            {
                String& string = fStrings[s];
                // one period ago, through the damping: the string's next sample, with the guitar pushing it
                const float back = fLines[s].readFractional(string.delay);
                string.lp += (back - string.lp) * string.lpCoef;
                const float y = drive * string.excite + string.feedback * string.lp;
                fLines[s].write(y);
                left += y * string.left;
                right += y * string.right;
            }
            const float level = fLevel.next();
            // the strings never clip, however hard they ring
            left = std::tanh(left) * level;
            right = std::tanh(right) * level;
            if (numChannels == 1)
            {
                io[0][i] += 0.5f * (left + right);
            }
            else
            {
                io[0][i] += left;
                io[1][i] += right;
            }
        }
    }

    void reset() override
    {
        for (DelayLine& line : fLines)
            line.reset();
        for (String& string : fStrings)
            string.lp = 0.0f;
        fHighPass.reset();
        fSnap = true;
    }

private:
    struct String {
        float delay = 100.0f;
        float feedback = 0.0f;
        float lpCoef = 1.0f; // the damping: 1 passes everything
        float lp = 0.0f;
        float excite = 0.0f;
        float left = 0.5f, right = 0.5f;
    };

    void updateStrings()
    {
        fUpdate = false;
        fInputGain = 0.4f * std::exp2(6.0f * fExcite - 3.0f);
        for (int s = 0; s < kStrings; ++s)
        {
            String& string = fStrings[s];
            const double midi = 48 + fKey + kTunings[fTuning][s] + kDetune[s] / 100.0;
            const double hz = std::max<double>(kMinHz, 440.0 * std::exp2((midi - 69.0) / 12.0));
            // the damping: a one-pole low-pass in the loop, from wide open down to about the 3rd harmonic
            const double cutoff = std::min(0.45 * fSampleRate, hz * std::exp2(7.0 * (1.0 - fDamping) + 1.5));
            const double c = 1.0 - std::exp(-2.0 * M_PI * cutoff / fSampleRate);
            string.lpCoef = static_cast<float>(c);
            // its delay (about (1 - c) / c samples at the note) comes off the loop's length
            const double lpDelay = (1.0 - c) / c;
            string.delay = static_cast<float>(std::max(2.0, fSampleRate / hz - lpDelay));
            string.feedback = static_cast<float>(std::pow(0.001, 1.0 / (std::max(0.05f, fDecay) * hz)));
            // a string with a long ring takes in less of each sample (its resonance builds higher)
            string.excite = 1.0f - string.feedback;
            const float pan = (s % 2 == 0 ? -1.0f : 1.0f) * fWidth;
            string.left = 0.5f * (1.0f - pan);
            string.right = 0.5f * (1.0f + pan);
        }
    }

    double fSampleRate = 48000.0;
    int fKey = 4;
    int fTuning = 0;
    float fDecay = 3.0f;
    float fDamping = 0.4f;
    float fExcite = 0.5f;
    float fWidth = 0.6f;
    float fInputGain = 0.4f;
    bool fUpdate = true;
    bool fSnap = true;
    Smoother fLevel;
    Biquad fHighPass;
    std::array<DelayLine, kStrings> fLines;
    std::array<String, kStrings> fStrings;
};

// ---------------------------------------------------------------------------------------------------------------------

class JawariBlock : public Block {
public:
    JawariBlock()
    {
        fMix.snap(1.0f);
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fLevel.prepare(sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            fOversampler[ch].prepare();
            fEnvelope[ch].prepare(sampleRate, 1.0, 150.0);
        }
        fPeakRelease = EnvelopeFollower::coef(sampleRate, 1500.0);
        updateColor();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kJawariParamBuzz:  fBuzz = value / 100.0f; break;
        case kJawariParamBloom: fBloom = value / 100.0f; break;
        case kJawariParamColor:
            fColor = value;
            updateColor();
            break;
        case kJawariParamMix:   fMix.setTarget(value / 100.0f); break;
        case kJawariParamLevel: fLevel.setTarget(dbToGain(value)); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
            fLevel.snap(fLevel.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mix = fMix.next(), level = fLevel.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                const float env = fEnvelope[ch].process(x);
                // the note's peak, held and let go slowly: how far the note has faded
                fPeak[ch] = env > fPeak[ch] ? env : fPeak[ch] * fPeakRelease;
                const float faded = fPeak[ch] > 1e-6f ? 1.0f - std::min(1.0f, env / fPeak[ch]) : 0.0f;
                // where the swing meets the bridge, as a share of the level: lower with Buzz, and lower still as the
                // note fades with Bloom
                const float contact = std::clamp(1.0f - fBuzz * (0.45f + 0.5f * fBloom * faded), 0.08f, 1.0f);
                const float bridge = std::max(1e-5f, env * contact);
                const float shaped = fOversampler[ch].process(x, [bridge](float v) {
                    // past the bridge the string folds back off its curve
                    return v <= bridge ? v : bridge - 0.7f * (v - bridge);
                });
                // with Bloom the buzz also grows louder as the note fades
                const float buzz = (shaped - x) * (1.0f + 1.5f * fBloom * faded);
                const float wet = x + 1.6f * buzz + 2.5f * fResonance[ch].process(buzz).band;
                io[ch][i] = (x + (wet - x) * mix) * level;
            }
        }
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fOversampler[ch].reset();
            fEnvelope[ch].reset();
            fResonance[ch].reset();
            fPeak[ch] = 0.0f;
        }
        fSnap = true;
    }

private:
    void updateColor()
    {
        for (Svf& filter : fResonance)
            filter.set(fSampleRate, std::min<double>(fColor, 0.45 * fSampleRate), 2.5);
    }

    double fSampleRate = 48000.0;
    float fBuzz = 0.6f, fBloom = 0.5f, fColor = 2500.0f;
    float fPeakRelease = 0.9999f;
    Smoother fMix, fLevel;
    Oversampler4x fOversampler[2];
    EnvelopeFollower fEnvelope[2];
    Svf fResonance[2];
    float fPeak[2] = { 0.0f, 0.0f };
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kSympatheticBlockDescriptor = {
    "sympathetic",
    "Sympathetic Strings",
    Category::Reverb,
    kSympParamCount,
    {
        ParamSpec { "Key", "", 0.0f, 11.0f, 4.0f, ParamScale::Choice, kKeyNames },
        ParamSpec { "Tuning", "", 0.0f, 4.0f, 0.0f, ParamScale::Choice, kTuningNames },
        ParamSpec { "Decay", "s", 0.3f, 10.0f, 3.0f, ParamScale::Log },
        ParamSpec { "Damping", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Excite", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Level", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SympatheticBlock()); },
};

const BlockDescriptor kJawariBlockDescriptor = {
    "jawari",
    "Jawari",
    Category::Distortion,
    kJawariParamCount,
    {
        ParamSpec { "Buzz", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Bloom", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Color", "Hz", 500.0f, 8000.0f, 2500.0f, ParamScale::Log },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new JawariBlock()); },
};

} // namespace xythos
