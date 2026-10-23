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
// peak picking sorts out. Lower bands ring up slowly (the time scales with Q over the frequency), so the octave
// below C3 widens to Q 5 at C2: notes a semitone apart aren't played down there anyway.
constexpr double kStageQ = 8.0;
constexpr double kLowStageQ = 5.0;
constexpr int kWideBands = 12;
constexpr float kFloor = 0.002f;    // band level a note needs (about -54 dBFS)
constexpr float kRelative = 0.12f;  // ... and against the loudest band (-18 dB)
constexpr float kHold = 1.4f;       // a playing note counts this much louder, so voices don't flicker
constexpr float kGain = 5.0f;       // band level to voice amplitude
constexpr float kVoiceLevel = 0.25f;
// A new note that falls below this share of its peak while the Pick Filter holds it was the ring of the pick
// attack, not a note: it starts waiting again
constexpr float kPickDecay = 0.6f;
constexpr double kFreqSmoothMs = 10.0; // the measured pitch (Quantize off)
// Semitones from a note up to its 2nd..8th harmonics (the 7th, 33.7, rounds to 34)
constexpr int kHarmonics[] = { 12, 19, 24, 28, 31, 34, 36 };
const double kSemitoneUp = std::exp2(1.0 / 12.0);
const double kSemitoneDown = 1.0 / kSemitoneUp;

class PolyChipBlock : public Block {
public:
    PolyChipBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fHop = std::max(1, static_cast<int>(std::lround(sampleRate / 1500.0)));
        fHopMs = 1000.0f * static_cast<float>(fHop / sampleRate);
        fAttackCoef = 1.0f - EnvelopeFollower::coef(sampleRate, 1.0);
        fFreqCoef = 1.0 - EnvelopeFollower::coef(sampleRate, kFreqSmoothMs);
        updateRelease();
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            const double hz = 440.0 * std::exp2((kFirstNote + b - 69) / 12.0);
            band.dt = hz / sampleRate;
            const double wide = std::max(0.0, 1.0 - static_cast<double>(b) / kWideBands);
            for (Svf& stage : band.stages)
                stage.set(sampleRate, hz, kStageQ + (kLowStageQ - kStageQ) * wide);
        }
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kPolyChipParamWave:    fWave = std::clamp(static_cast<int>(value), 0, 3); break;
        case kPolyChipParamVoices:  fVoices = std::clamp(static_cast<int>(value), 1, kMaxVoices); break;
        // a lower peak at a harmonic's distance drops this one when it's louder than 1/ratio of it: from 1/8
        // (only much louder notes) at 0% to 8 (harmonics 18 dB over their note) at 100%; a guitar's 2nd harmonic
        // is often twice its note, more while the note's own band rings up
        case kPolyChipParamFocus:   fFocusRatio = std::exp2(6.0f * value / 100.0f - 3.0f); break;
        case kPolyChipParamArp:     fArp = static_cast<PolyChipArp>(std::clamp(static_cast<int>(value), 0, 2)); break;
        case kPolyChipParamArpRate: fArpHz = value; break;
        case kPolyChipParamRelease:
            fReleaseMs = value;
            updateRelease();
            break;
        case kPolyChipParamCrush:   fCrush = value / 100.0f; break;
        case kPolyChipParamOctave:  fOctaveRatio = std::exp2(std::clamp(static_cast<int>(value), 0, 3) - 2); break;
        case kPolyChipParamMix:     fMix.setTarget(value / 100.0f); break;
        case kPolyChipParamQuantize: fQuantize = value >= 0.5f; break;
        case kPolyChipParamPickFilter: fPickMs = value; break;
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
                if (band.sounding || band.amp > 0.0f)
                    measurePitch(band, out);
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
            band.bp = band.lp = band.level = band.amp = band.target = band.pending = 0.0f;
            band.phase = 0.0;
            band.measured = band.dt;
            band.on = band.sounding = false;
            band.waited = 0;
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
        double measured = 0.0; // the pitch in the band, as a phase step per sample (for Quantize off)
        bool on = false;       // one of the notes found
        bool sounding = false; // ... and past the Pick Filter
        int waited = 0;        // hops it has been found while the Pick Filter holds it
        float pending = 0.0f;  // its peak level meanwhile
    };

    // The band's pitch from how fast its quarter-cycle pair turns. Away from the band's note the pair's sizes differ
    // a little, so the turn rate ripples within each cycle; the smoothing evens that out.
    void measurePitch(Band& band, const Svf::Out& out) const
    {
        const double cross = static_cast<double>(band.lp) * out.band - static_cast<double>(band.bp) * out.low;
        const double dot = static_cast<double>(band.lp) * out.low + static_cast<double>(band.bp) * out.band;
        if (cross == 0.0 && dot == 0.0)
            return;
        const double step = std::fabs(std::atan2(cross, dot)) / (2.0 * M_PI);
        band.measured += (step - band.measured) * fFreqCoef;
        // within a semitone of the band's own note (a stronger neighbour can pull it further)
        band.measured = std::clamp(band.measured, band.dt * kSemitoneDown, band.dt * kSemitoneUp);
    }

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

        // the Pick Filter: a new note sounds once it has lasted that long without dying away, so the brief ring of
        // the pick attack in the higher bands (they speak faster than the note's own band) never sounds
        int playing = 0;
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            if (!band.on)
            {
                band.sounding = false;
                band.waited = 0;
                band.pending = 0.0f;
            }
            else if (!band.sounding)
            {
                band.pending = std::max(band.pending, band.level);
                if (band.level < kPickDecay * band.pending)
                {
                    band.waited = 0;
                    band.pending = band.level;
                }
                else if (static_cast<float>(++band.waited) * fHopMs >= fPickMs)
                {
                    band.sounding = true;
                    takeOverVoice(b);
                }
            }
            band.target = band.sounding ? std::min(1.0f, band.level * kGain) : 0.0f;
            playing += band.sounding ? 1 : 0;
        }
        if (playing == 0 && fPlaying > 0)
        {
            // the next chord starts its arpeggio from the start
            fArpPhase = 0.0;
            fArpStep = 0;
        }
        fPlaying = playing;
    }

    // A note that starts next to one that is fading (a bend or slide into the next semitone) carries on that voice,
    // its phase and level, instead of starting a second voice against it
    void takeOverVoice(int b)
    {
        int from = -1;
        for (int d = -2; d <= 2; ++d)
        {
            const int n = b + d;
            if (d == 0 || n < 0 || n >= kBands || fBands[n].sounding || fBands[n].amp <= 0.0f)
                continue;
            if (from < 0 || fBands[n].amp > fBands[from].amp)
                from = n;
        }
        if (from < 0)
            return;
        Band& band = fBands[b];
        Band& old = fBands[from];
        band.phase = old.phase;
        band.amp = old.amp;
        band.measured = std::clamp(old.measured, band.dt * kSemitoneDown, band.dt * kSemitoneUp);
        old.amp = 0.0f;
        old.phase = 0.0;
        old.measured = old.dt;
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
            if (fBands[b].sounding && index-- == 0)
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
            const bool fast = target > band.amp || (arp >= 0 && band.sounding);
            band.amp += (target - band.amp) * (fast ? fAttackCoef : fReleaseCoef);
            if (target == 0.0f && band.amp < 1e-5f)
            {
                band.amp = 0.0f;
                band.phase = 0.0;
                band.measured = band.dt;
                continue;
            }
            const double dt = (fQuantize ? band.dt : band.measured) * fOctaveRatio;
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
    bool fQuantize = true;
    float fPickMs = 15.0f;

    std::array<Band, kBands> fBands;
    Smoother fMix;
    int fHop = 32, fSinceHop = 0;
    float fHopMs = 0.67f;
    double fFreqCoef = 1.0;
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
        ParamSpec { "Quantize", "", 0.0f, 1.0f, 1.0f, ParamScale::Toggle },
        ParamSpec { "Pick Filter", "ms", 0.0f, 60.0f, 15.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new PolyChipBlock()); },
};

} // namespace namsayin
