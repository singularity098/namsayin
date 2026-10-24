// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/OnsetDetector.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace namsayin {

// Finds the notes of chords (the Poly Chip and the MIDI Out's Poly mode): one band per semitone, C2..E6 (53 bands,
// each two SVF band-passes in series at Q 8, widening to Q 5 over the octave below C3 so the low strings ring up
// faster). Every ~0.7 ms (a hop): local peaks over the floor and within 18 dB of the loudest band are candidates;
// Focus drops a peak when one at a harmonic's distance below is louder than 1/Focus of it; the loudest Voices remain
// (playing notes counted 3 dB louder, so they don't flicker). The Pick Filter: a new note sounds once found for that
// long without falling below 60% of its peak meanwhile (the pick attack's ring in the higher bands dies away). Each
// band also measures the pitch in it (from the turn rate of its band-pass and low-pass outputs, a quarter cycle
// apart at its note), for bends.
//
// Like the single-note tracker (NoteTracker.hpp), it also follows the input's own level (a peak follower that holds
// for the lowest note's period, then falls fast) and its noise floor: a band louder than the input itself is only
// ringing on in its filters (after a mute), so its note ends at once, and notes need to stand over the noise floor.
// A playing note keeps a head start over its neighbours (no flicker), a playing note that falls away fast just
// after a pick was muted for the new one, and a note picked
// again (a pick, then its band's level jumping) starts afresh (Frame::start without a gap, for MIDI).
//
// Each hop's result is a Frame. With lookahead the frames are played that many whole hops later, and a new note is
// written back over its pick-filter wait and its band's ring-up, so it starts on time.
class PolyNoteFinder {
public:
    static constexpr int kFirstNote = 36; // C2 (MIDI), below drop tunings
    static constexpr int kLastNote = 88;  // E6, the top of a 24-fret guitar
    static constexpr int kBands = kLastNote - kFirstNote + 1;
    static constexpr int kMaxVoices = 6;
    static constexpr double kMaxLookaheadMs = 30.0;

    struct Frame {
        std::array<float, kBands> level {};     // each band's level
        std::array<double, kBands> measured {}; // the pitch in it, as a phase step per sample (0: not measured)
        std::array<bool, kBands> sounding {};   // a note plays in it
        std::array<bool, kBands> start {};      // ... and starts in this frame
        std::array<float, kBands> velocity {};  // the starting note's level (when its start is set)
    };

    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fHop = std::max(1, static_cast<int>(std::lround(sampleRate / 1500.0)));
        fHopMs = 1000.0f * static_cast<float>(fHop / sampleRate);
        fFreqCoef = 1.0 - EnvelopeFollower::coef(sampleRate, kFreqSmoothMs);
        fFrames.assign(static_cast<size_t>(std::ceil(kMaxLookaheadMs / fHopMs)) + 2, Frame {});
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            const double hz = 440.0 * std::exp2((kFirstNote + b - 69) / 12.0);
            band.dt = hz / sampleRate;
            const double wide = std::max(0.0, 1.0 - static_cast<double>(b) / kWideBands);
            const double q = kStageQ + (kLowStageQ - kStageQ) * wide;
            for (Svf& stage : band.stages)
                stage.set(sampleRate, hz, q);
            // how long the band takes to ring up to a note (two stages, each about Q / (pi f))
            fRingUpHops[b] = static_cast<int>(std::lround(1.5 * q / (M_PI * hz) * 1000.0 / fHopMs));
        }
        fOnset.prepare(sampleRate);
        fClick.prepare(sampleRate);
        fClick.setThreshold(0.002f);
        fClickFilter.setHighPass(sampleRate, 2000.0);
        fEnvAttack = 1.0f - EnvelopeFollower::coef(sampleRate, 0.3);
        fEnvRelease = EnvelopeFollower::coef(sampleRate, 4.0);
        fEnvHold = static_cast<int>(sampleRate / 60.0);
        fFloorFall = 1.0f - EnvelopeFollower::coef(sampleRate, 20.0);
        fFloorRise = static_cast<float>(std::pow(10.0, 6.0 / 20.0 / sampleRate));
        fRepickHops = static_cast<int>(std::lround(kRepickMs / fHopMs));
        fMutedHops = static_cast<int>(std::lround(kMutedMs / fHopMs));
        setLookahead(fLookaheadMs);
        reset();
    }

    // Non-RT (it changes the latency)
    void setLookahead(double ms)
    {
        fLookaheadMs = std::clamp(ms, 0.0, kMaxLookaheadMs);
        if (!fFrames.empty())
            fDelayHops = std::clamp(static_cast<int>(std::lround(fLookaheadMs / fHopMs)), 0,
                                    static_cast<int>(fFrames.size()) - 2);
    }

    uint32_t latency() const { return static_cast<uint32_t>(fDelayHops * fHop); }
    void setPickFilter(float ms) { fPickMs = ms; }
    void setFocus(float ratio) { fFocusRatio = ratio; }
    void setVoices(int voices) { fVoices = std::clamp(voices, 1, kMaxVoices); }

    // How many picks it has heard (for tests)
    int picks() const { return fPicks; }

    // The phase step per sample of a band's own semitone
    double bandStep(int b) const { return fBands[b].dt; }

    void reset()
    {
        for (Band& band : fBands)
        {
            for (Svf& stage : band.stages)
                stage.reset();
            band.bp = band.lp = band.level = band.pending = 0.0f;
            band.measured = band.dt;
            band.on = band.sounding = false;
            band.waited = 0;
            band.recent = 0;
            band.atPick = 0.0f;
        }
        fOnset.reset();
        fClick.reset();
        fClickFilter.reset();
        fEnvelope = 0.0f;
        fHold = 0;
        fFloor = 0.0003f;
        fSincePick = 1 << 20;
        fPickHop = 0;
        fPicks = 0;
        std::fill(fFrames.begin(), fFrames.end(), Frame {});
        fHopCount = 0;
        fSinceHop = 0;
    }

    // One input sample; true when a new frame is ready to play (playFrame())
    bool process(float in)
    {
        followLevel(in);
        // a pick: a jump in the level, or in the pick's click (high-passed), which stands out while notes ring
        bool picked = fOnset.process(std::fabs(in));
        picked = fClick.process(std::fabs(fClickFilter.process(in))) || picked;
        if (picked)
        {
            fPickPending = true;
            ++fPicks;
        }
        for (Band& band : fBands)
        {
            const float first = band.stages[0].process(in).band * band.stages[0].k();
            const Svf::Out out = band.stages[1].process(first);
            if (band.on || band.sounding || band.recent > 0)
                measurePitch(band, out);
            band.bp = out.band;
            band.lp = out.low;
        }
        if (++fSinceHop < fHop)
            return false;
        fSinceHop = 0;
        pickNotes();
        return true;
    }

    // The frame to play now (the newest, or the one the lookahead behind)
    const Frame& playFrame() { return frame(fHopCount - 1 - fDelayHops); }

private:
    struct Band {
        std::array<Svf, 2> stages;
        double dt = 0.0;   // phase step per sample at its note
        float bp = 0.0f, lp = 0.0f; // the second stage's outputs, a quarter cycle apart at the band's note
        float level = 0.0f; // measured each hop
        double measured = 0.0;
        bool on = false;       // one of the notes found
        bool sounding = false; // ... and past the Pick Filter
        int waited = 0;        // hops it has been found while the Pick Filter holds it
        float pending = 0.0f;  // its peak level meanwhile
        int recent = 0;        // hops it keeps measuring its pitch after it stops (the voice's release)
        float atPick = 0.0f;   // its level at the last pick (a jump from it: picked again)
    };

    // The input's level: a peak follower holding for the lowest note's period, then falling fast; and its noise
    // floor (down quickly, up slowly, only while nothing sounds)
    void followLevel(float in)
    {
        const float a = std::fabs(in);
        if (a > fEnvelope)
        {
            fEnvelope += (a - fEnvelope) * fEnvAttack;
            fHold = fEnvHold;
        }
        else if (fHold > 0)
        {
            --fHold;
        }
        else
        {
            fEnvelope *= fEnvRelease;
        }
        if (fEnvelope < fFloor)
            fFloor += (fEnvelope - fFloor) * fFloorFall;
        else if (fSounding == 0)
            fFloor = std::min(0.03f, fFloor * fFloorRise);
    }

    static constexpr double kStageQ = 8.0;
    static constexpr double kLowStageQ = 5.0;
    static constexpr int kWideBands = 12;
    static constexpr float kFloor = 0.002f;    // band level a note needs (about -54 dBFS)
    static constexpr float kRelative = 0.12f;  // ... and against the loudest band (-18 dB)
    static constexpr float kHold = 1.4f;       // a playing note counts this much louder, so voices don't flicker
    static constexpr float kPickDecay = 0.6f;  // a new note falling below this share of its peak while held: the pick
    static constexpr double kFreqSmoothMs = 10.0;
    static constexpr int kRecentHops = 150;    // ~100 ms
    static constexpr double kRepickMs = 15.0;  // after a pick, a jump in a sounding band's level within this: picked again
    static constexpr float kRepickJump = 1.6f;
    static constexpr double kMutedMs = 40.0;   // after a pick, a note falling under kMutedFall of its level within
    static constexpr float kMutedFall = 0.7f;  // this long was muted
    static constexpr float kStale = 1.5f;      // a band this much louder than the input is ringing on by itself
    static constexpr float kFloorGate = 4.0f;  // notes stand 12 dB over the noise floor
    // Semitones from a note up to its 2nd..8th harmonics (the 7th, 33.7, rounds to 34)
    static constexpr int kHarmonics[7] = { 12, 19, 24, 28, 31, 34, 36 };

    Frame& frame(int64_t hop)
    {
        const auto size = static_cast<int64_t>(fFrames.size());
        return fFrames[static_cast<size_t>(((hop % size) + size) % size)];
    }

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
        const double up = std::exp2(1.0 / 12.0);
        band.measured = std::clamp(band.measured, band.dt / up, band.dt * up);
    }

    void pickNotes()
    {
        const float floor = std::max(kFloor, kFloorGate * fFloor);
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
            // (a playing note keeps its head start against its neighbours too, so it doesn't flicker)
            const float self = fBands[b].level * (fBands[b].sounding ? kHold : 1.0f);
            peak[b] = self > below && self >= above && level > floor && level > kRelative * loudest
                      && !stale(fBands[b]);
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

        // a pick: remember each sounding note's level, to tell a fresh pick of it from its ringing on
        // (the strings of a strum are several picks a few ms apart: the levels are the first one's)
        if (fPickPending)
        {
            fPickPending = false;
            if (fSincePick > fMutedHops)
                for (Band& band : fBands)
                    band.atPick = band.sounding ? band.level : 0.0f;
            fSincePick = 0;
            fPickHop = fHopCount;
        }
        else
        {
            ++fSincePick;
        }

        Frame& now = frame(fHopCount);
        now = Frame {};
        fSounding = 0;
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            bool started = false;
            // after a pick, a note falling away fast was muted for the new one (a let-ring note decays slowly)
            const bool muted = band.sounding && band.atPick > 0.0f && fSincePick <= fMutedHops
                               && band.level < kMutedFall * band.atPick;
            if (!band.on || muted)
            {
                if (band.sounding)
                    band.recent = kRecentHops;
                band.sounding = false;
                band.waited = 0;
                band.pending = 0.0f;
            }
            else if (band.sounding && band.atPick > 0.0f && fSincePick <= fRepickHops
                     && band.level > kRepickJump * band.atPick)
            {
                // picked again: a fresh start at the pick (with lookahead, back where it was)
                band.atPick = 0.0f;
                const int64_t at = std::max(fPickHop, fHopCount - fDelayHops);
                frame(at).start[b] = true;
                frame(at).velocity[b] = band.level;
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
                    started = true;
                }
            }
            if (band.recent > 0)
                --band.recent;
            fSounding += band.sounding ? 1 : 0;
            now.level[b] = band.level;
            now.measured[b] = band.measured;
            now.sounding[b] = band.sounding;
            if (started)
            {
                // with lookahead the note starts where it began: back over its wait and its band's ring-up, in the
                // frames not yet played
                // (to the pick that started it; a note with no pick that recent only over its wait)
                int back = band.waited + fRingUpHops[b];
                back = fSincePick <= back ? fSincePick : band.waited;
                back = std::min(back, fDelayHops);
                int64_t first = fHopCount;
                for (int k = 1; k <= back; ++k)
                {
                    Frame& past = frame(fHopCount - k);
                    if (past.sounding[b])
                        break;
                    past.sounding[b] = true;
                    past.measured[b] = band.measured;
                    first = fHopCount - k;
                }
                Frame& at = frame(first);
                at.start[b] = true;
                at.velocity[b] = band.pending > 0.0f ? std::max(band.pending, band.level) : band.level;
            }
        }
        ++fHopCount;
    }

    double fSampleRate = 48000.0;
    int fHop = 32, fSinceHop = 0;
    float fHopMs = 0.67f;
    double fFreqCoef = 1.0;
    float fPickMs = 15.0f;
    float fFocusRatio = 1.0f;
    int fVoices = 4;
    double fLookaheadMs = 0.0;
    int fDelayHops = 0;
    std::array<Band, kBands> fBands;
    std::array<int, kBands> fRingUpHops {};
    std::vector<Frame> fFrames;
    int64_t fHopCount = 0;

    bool stale(const Band& band) const { return band.level > kStale * fEnvelope; }

    OnsetDetector fOnset, fClick;
    Biquad fClickFilter;
    bool fPickPending = false;
    int fSincePick = 1 << 20;
    int64_t fPickHop = 0;
    int fRepickHops = 22, fMutedHops = 45;
    float fEnvelope = 0.0f, fFloor = 0.0003f;
    float fEnvAttack = 1.0f, fEnvRelease = 0.99f, fFloorFall = 0.001f, fFloorRise = 1.0f;
    int fHold = 0, fEnvHold = 800;
    int fSounding = 0;
    int fPicks = 0;
};

} // namespace namsayin
