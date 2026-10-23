// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/PolyChipBlock.hpp"

#include "engine/dsp/Oscillator.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kWaveNames[] = { "Pulse 12%", "Pulse 25%", "Pulse 50%", "Triangle" };
constexpr double kWidths[] = { 0.125, 0.25, 0.5 };
constexpr const char* kVoiceNames[] = { "1", "2", "3", "4", "5", "6" };
constexpr const char* kArpNames[] = { "Off", "Up", "Down" };
constexpr const char* kOctaveNames[] = { "-2", "-1", "0", "+1" };

constexpr int kFirstNote = 36; // C2 (MIDI), below drop tunings
constexpr int kLastNote = 88;  // E6, the top of a 24-fret guitar
constexpr int kBands = kLastNote - kFirstNote + 1;
constexpr int kMaxVoices = 6;
// Each band is two band-passes in series. Q 8 each: a note a semitone away comes through about 5 dB down, which the
// peak picking sorts out, while the low E still speaks within about 20 ms.
constexpr double kStageQ = 8.0;
constexpr float kFloor = 0.002f;    // band level a note needs (about -54 dBFS)
constexpr float kRelative = 0.06f;  // ... and against the loudest band (-24 dB)
constexpr float kHold = 1.4f;       // a playing note counts this much louder, so voices don't flicker
constexpr float kGain = 5.0f;       // band level to voice amplitude
constexpr float kVoiceLevel = 0.25f;
// Semitones from a note up to its 2nd..8th harmonics (the 7th, 33.7, rounds to 34)
constexpr int kHarmonics[] = { 12, 19, 24, 28, 31, 34, 36 };

class PolyChipBlock : public Block {
public:
    PolyChipBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fHop = std::max(1, static_cast<int>(std::lround(sampleRate / 1500.0)));
        fAttackCoef = 1.0f - EnvelopeFollower::coef(sampleRate, 1.0);
        updateRelease();
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            const double hz = 440.0 * std::exp2((kFirstNote + b - 69) / 12.0);
            band.dt = hz / sampleRate;
            for (Svf& stage : band.stages)
                stage.set(sampleRate, hz, kStageQ);
        }
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kPolyChipParamWave:    fWave = std::clamp(static_cast<int>(value), 0, 3); break;
        case kPolyChipParamVoices:  fVoices = std::clamp(static_cast<int>(value), 1, kMaxVoices); break;
        // a lower peak at a harmonic's distance drops this one when it's louder than 1/ratio of it: from 1/16
        // (only much louder notes) at 0% to 4 (harmonics 12 dB over their note) at 100%
        case kPolyChipParamFocus:   fFocusRatio = std::exp2(6.0f * value / 100.0f - 4.0f); break;
        case kPolyChipParamArp:     fArp = static_cast<PolyChipArp>(std::clamp(static_cast<int>(value), 0, 2)); break;
        case kPolyChipParamArpRate: fArpHz = value; break;
        case kPolyChipParamRelease:
            fReleaseMs = value;
            updateRelease();
            break;
        case kPolyChipParamCrush:   fCrush = value / 100.0f; break;
        case kPolyChipParamOctave:  fOctaveRatio = std::exp2(std::clamp(static_cast<int>(value), 0, 3) - 2); break;
        case kPolyChipParamMix:     fMix.setTarget(value / 100.0f); break;
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
            const float mono = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            for (Band& band : fBands)
            {
                const float first = band.stages[0].process(mono).band * band.stages[0].k();
                const Svf::Out out = band.stages[1].process(first);
                band.bp = out.band;
                band.lp = out.low;
            }
            if (++fSinceHop >= fHop)
            {
                fSinceHop = 0;
                pickNotes();
            }
            if (fArp != PolyChipArp::Off && fPlaying > 0 && advancePhase(fArpPhase, fArpHz / fSampleRate))
                ++fArpStep;

            const float synth = render();
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] = io[ch][i] * (1.0f - mix) + synth * mix;
        }
    }

    void reset() override
    {
        for (Band& band : fBands)
        {
            for (Svf& stage : band.stages)
                stage.reset();
            band.bp = band.lp = band.level = band.amp = band.target = 0.0f;
            band.phase = 0.0;
            band.on = false;
        }
        fSinceHop = 0;
        fPlaying = 0;
        fArpPhase = 0.0;
        fArpStep = 0;
        fSnap = true;
    }

private:
    struct Band {
        std::array<Svf, 2> stages;
        double dt = 0.0;   // phase step per sample at octave 0
        float bp = 0.0f, lp = 0.0f; // the second stage's outputs, a quarter cycle apart at the band's note
        float level = 0.0f; // the band's amplitude, measured each hop
        float target = 0.0f, amp = 0.0f;
        double phase = 0.0;
        bool on = false;   // one of the notes being played
    };

    // Every hop: measure the bands, find the notes and set each voice's target level
    void pickNotes()
    {
        float loudest = 0.0f;
        for (Band& band : fBands)
        {
            // at the band's note the band-pass and low-pass outputs have equal size and are a quarter cycle apart,
            // so together they give the amplitude without ripple; k makes the peak gain 1
            band.level = band.stages[1].k() * std::sqrt(band.bp * band.bp + band.lp * band.lp);
            loudest = std::max(loudest, band.level);
        }

        // peaks above the floors are candidate notes
        std::array<bool, kBands> peak {};
        for (int b = 0; b < kBands; ++b)
        {
            const float level = fBands[b].level * (fBands[b].on ? kHold : 1.0f);
            const float below = b > 0 ? fBands[b - 1].level : 0.0f;
            const float above = b + 1 < kBands ? fBands[b + 1].level : 0.0f;
            peak[b] = fBands[b].level > below && fBands[b].level >= above && level > kFloor
                      && level > kRelative * loudest;
        }

        // drop the harmonics of lower notes, then keep the loudest few
        std::array<int, kMaxVoices> chosen {};
        int count = 0;
        for (int b = 0; b < kBands; ++b)
        {
            if (!peak[b])
                continue;
            bool harmonic = false;
            for (const int d : kHarmonics)
                if (b - d >= 0 && peak[b - d] && fBands[b - d].level * fFocusRatio > fBands[b].level)
                    harmonic = true;
            if (harmonic)
                continue;
            const float strength = fBands[b].level * (fBands[b].on ? kHold : 1.0f);
            if (count < fVoices)
            {
                chosen[count++] = b;
                continue;
            }
            int weakest = 0;
            for (int v = 1; v < count; ++v)
                if (fBands[chosen[v]].level < fBands[chosen[weakest]].level)
                    weakest = v;
            const Band& old = fBands[chosen[weakest]];
            if (strength > old.level * (old.on ? kHold : 1.0f))
                chosen[weakest] = b;
        }

        for (Band& band : fBands)
            band.on = false;
        for (int v = 0; v < count; ++v)
            fBands[chosen[v]].on = true;
        for (Band& band : fBands)
            band.target = band.on ? std::min(1.0f, band.level * kGain) : 0.0f;
        if (count == 0 && fPlaying > 0)
        {
            // the next chord starts its arpeggio from the start
            fArpPhase = 0.0;
            fArpStep = 0;
        }
        fPlaying = count;
    }

    // The band the arpeggio is on, or -1 when every playing note sounds
    int arpBand() const
    {
        if (fArp == PolyChipArp::Off || fPlaying == 0)
            return -1;
        int index = fArpStep % fPlaying;
        if (fArp == PolyChipArp::Down)
            index = fPlaying - 1 - index;
        for (int b = 0; b < kBands; ++b)
            if (fBands[b].on && index-- == 0)
                return b;
        return -1;
    }

    float render()
    {
        const int arp = arpBand();
        float sum = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            const float target = arp < 0 || arp == b ? band.target : 0.0f;
            if (target == 0.0f && band.amp == 0.0f)
                continue;
            // notes the arpeggio has moved off stop at once, like the console switching notes
            const bool fast = target > band.amp || (arp >= 0 && band.on);
            band.amp += (target - band.amp) * (fast ? fAttackCoef : fReleaseCoef);
            if (target == 0.0f && band.amp < 1e-5f)
            {
                band.amp = 0.0f;
                band.phase = 0.0;
                continue;
            }
            const double dt = band.dt * fOctaveRatio;
            if (dt >= 0.45)
                continue;
            advancePhase(band.phase, dt);
            float osc;
            if (fWave == 3)
            {
                // the console's triangle: 16 steps
                const double tri = band.phase < 0.5 ? 4.0 * band.phase - 1.0 : 3.0 - 4.0 * band.phase;
                osc = static_cast<float>(std::round(tri * 7.5) / 7.5);
            }
            else
            {
                osc = blepPulse(band.phase, dt, kWidths[fWave]);
            }
            // with Crush, the console's 16 volume steps
            const float amp = fCrush > 0.0f ? std::round(band.amp * 15.0f) / 15.0f : band.amp;
            sum += osc * amp * kVoiceLevel;
        }
        if (fCrush > 0.0f)
        {
            // fewer bits for the samples as Crush goes up
            const float step = std::exp2(-(15.0f - 11.0f * fCrush));
            sum = std::round(sum / step) * step;
        }
        return sum;
    }

    void updateRelease() { fReleaseCoef = 1.0f - EnvelopeFollower::coef(fSampleRate, std::max(1.0f, fReleaseMs)); }

    double fSampleRate = 48000.0;
    int fWave = 1;
    int fVoices = 4;
    float fFocusRatio = 1.0f;
    PolyChipArp fArp = PolyChipArp::Off;
    float fArpHz = 15.0f;
    float fReleaseMs = 40.0f;
    float fCrush = 0.3f;
    double fOctaveRatio = 1.0;

    std::array<Band, kBands> fBands;
    Smoother fMix;
    int fHop = 32, fSinceHop = 0;
    int fPlaying = 0;
    double fArpPhase = 0.0;
    int fArpStep = 0;
    float fAttackCoef = 1.0f, fReleaseCoef = 1.0f;
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kPolyChipBlockDescriptor = {
    "polychip",
    "Poly Chip",
    Category::PitchSynth,
    kPolyChipParamCount,
    {
        ParamSpec { "Wave", "", 0.0f, 3.0f, 1.0f, ParamScale::Choice, kWaveNames },
        ParamSpec { "Voices", "", 1.0f, 6.0f, 4.0f, ParamScale::Choice, kVoiceNames },
        ParamSpec { "Focus", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Arp", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kArpNames },
        ParamSpec { "Arp Rate", "Hz", 2.0f, 40.0f, 15.0f, ParamScale::Log },
        ParamSpec { "Release", "ms", 10.0f, 2000.0f, 40.0f, ParamScale::Log },
        ParamSpec { "Crush", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Octave", "", 0.0f, 3.0f, 2.0f, ParamScale::Choice, kOctaveNames },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new PolyChipBlock()); },
};

} // namespace namsayin
