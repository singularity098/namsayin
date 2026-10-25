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
// faster), and above them up to C8 for their harmonics. Every ~0.7 ms (a hop) it finds the notes one at a time
// (Klapuri's iterative estimation): each candidate scores the weighted sum of its first eight harmonics' levels; the
// best is a note, preferring one an octave, a twelfth or two octaves below that is there too (the low strings'
// fundamentals are weak in a DI signal: the owner's open G has G3 12 dB over G2), and what it explains is taken out
// before the next: its fundamental and odd harmonics, its even ones up to Focus times their odd neighbours (more is a
// note an octave up), and what the neighbouring bands let through of them. A candidate's own band must hold a local
// peak and stand over the floors; it must not be much weaker than its harmonics, a note below a played one has only
// even harmonics there, and its first partials must be within 9 dB of the loudest band. Up to Voices notes; playing
// notes count 6 dB louder, so they don't flicker. The Pick Filter: a new note sounds once found for that long without
// falling below 60% of its peak meanwhile (the pick attack's ring in the higher bands dies away). Each band also
// measures the pitch in it (from the turn rate of its band-pass and low-pass outputs, a quarter cycle apart at its
// note), for bends.
//
// Like the single-note tracker (NoteTracker.hpp), it also follows the input's own level (a peak follower that holds
// for the lowest note's period, then falls fast) and its noise floor: a band louder than the input itself is only
// ringing on in its filters (after a mute), so its note ends at once. Each band also has a noise floor of its own
// (learnt in the first 250 ms while the input is quiet, then down quickly and up slowly), and a note needs to stand
// 12 dB over its band's, so the hiss and hum of a live input don't cut short notes well over them (the input's own
// floor only gates at half).
// A playing note keeps a head start over its neighbours, and one that is briefly not found (a harmonic of another
// note outscoring it for a moment) keeps sounding for up to 40 ms unless its level falls meanwhile, a pick comes or
// a neighbour takes over (a bend or slide), so it doesn't flicker. A playing note that falls away fast just after a
// pick was muted for the new one, and a note picked again (a pick, then its band's level jumping) starts afresh
// (Frame::start without a gap, for MIDI).
//
// Each hop's result is a Frame. With lookahead the frames are played that many whole hops later, and a new note is
// written back over its pick-filter wait and its band's ring-up, so it starts on time.
class PolyNoteFinder {
public:
    static constexpr int kFirstNote = 36; // C2 (MIDI), below drop tunings
    static constexpr int kLastNote = 88;  // E6, the top of a 24-fret guitar
    static constexpr int kBands = kLastNote - kFirstNote + 1;
    static constexpr int kMaxVoices = 6;
    // the bands measured: the notes' and, above them, their harmonics' (up to C8)
    static constexpr int kLastMeasured = 108;
    static constexpr int kMeasured = kLastMeasured - kFirstNote + 1;
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
        for (int m = 0; m < kMeasured; ++m)
        {
            const double hz = 440.0 * std::exp2((kFirstNote + m - 69) / 12.0);
            const double wide = std::max(0.0, 1.0 - static_cast<double>(m) / kWideBands);
            const double q = kStageQ + (kLowStageQ - kStageQ) * wide;
            for (Svf& stage : fFilters[m].stages)
                stage.set(sampleRate, hz, q);
            // how much of a note a semitone away the band lets through (two stages)
            const double off = std::exp2(1.0 / 12.0) - std::exp2(-1.0 / 12.0);
            fLeak[m] = static_cast<float>(1.0 / (1.0 + q * q * off * off));
            if (m >= kBands)
                continue;
            Band& band = fBands[m];
            band.dt = hz / sampleRate;
            // how long the band takes to ring up to a note (two stages, each about Q / (pi f))
            fRingUpHops[m] = static_cast<int>(std::lround(1.5 * q / (M_PI * hz) * 1000.0 / fHopMs));
            // how much each harmonic counts towards the note (Klapuri's weights: the low ones count more for high
            // notes, about the same for low notes, whose fundamental is often weak)
            for (int h = 0; h < kPartials; ++h)
                fWeight[m][h] = static_cast<float>((hz + 52.0) / ((h + 1) * hz + 320.0));
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
        fBandFloorFall = 1.0f - static_cast<float>(std::exp(-fHopMs / kBandFloorFallMs));
        for (int r = 0; r < 3; ++r)
            fBandFloorRise[r] = static_cast<float>(std::pow(10.0, kBandFloorRiseDb[r] / 20.0 * fHopMs / 1000.0));
        fLearnHops = static_cast<int>(std::lround(kLearnMs / fHopMs));
        fLearnCoef = 1.0f - static_cast<float>(std::exp(-fHopMs / 30.0));
        fLostHops = static_cast<int>(std::lround(kLostMs / fHopMs));
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
        for (Filter& filter : fFilters)
        {
            for (Svf& stage : filter.stages)
                stage.reset();
            filter.bp = filter.lp = 0.0f;
        }
        for (Band& band : fBands)
        {
            band.level = band.pending = 0.0f;
            band.measured = band.dt;
            band.on = band.sounding = false;
            band.waited = 0;
            band.recent = 0;
            band.lost = 0;
            band.lostLevel = 0.0f;
            band.atPick = 0.0f;
            band.floor = kStartFloor;
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
        fLearning = true;
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
        for (int m = 0; m < kMeasured; ++m)
        {
            Filter& filter = fFilters[m];
            const float first = filter.stages[0].process(in).band * filter.stages[0].k();
            const Svf::Out out = filter.stages[1].process(first);
            if (m < kBands && (fBands[m].on || fBands[m].sounding || fBands[m].recent > 0))
                measurePitch(fBands[m], filter, out);
            filter.bp = out.band;
            filter.lp = out.low;
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
    struct Filter {
        std::array<Svf, 2> stages;
        float bp = 0.0f, lp = 0.0f; // the second stage's outputs, a quarter cycle apart at the band's note
    };

    struct Band {
        double dt = 0.0;   // phase step per sample at its note
        float level = 0.0f; // the note's level, measured each hop
        double measured = 0.0;
        bool on = false;       // one of the notes found
        bool sounding = false; // ... and past the Pick Filter
        int waited = 0;        // hops it has been found while the Pick Filter holds it
        float pending = 0.0f;  // its peak level meanwhile
        int recent = 0;        // hops it keeps measuring its pitch after it stops (the voice's release)
        int lost = 0;          // hops a sounding note hasn't been found
        float lostLevel = 0.0f; // its level when it was lost
        float floor = 0.0f;    // its noise floor
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
    static constexpr float kFloor = 0.0002f;   // band level a note needs (about -74 dBFS; DI recordings are quiet)
    static constexpr float kRelative = 0.12f;  // a note's score against the first note's (-18 dB)
    static constexpr float kRelativeLevel = 0.35f; // its first three partials against the loudest band (-9 dB)
    static constexpr float kHold = 2.0f;       // a playing note counts this much louder, so voices don't flicker
    static constexpr float kPickDecay = 0.6f;  // a new note falling below this share of its peak while held: the pick
    static constexpr double kFreqSmoothMs = 10.0;
    static constexpr int kRecentHops = 150;    // ~100 ms
    static constexpr double kRepickMs = 15.0;  // after a pick, a jump in a sounding band's level within this: picked again
    static constexpr float kRepickJump = 1.6f;
    static constexpr double kMutedMs = 40.0;   // after a pick, a note falling under kMutedFall of its level within
    static constexpr float kMutedFall = 0.7f;  // this long was muted
    static constexpr float kStale = 1.5f;      // a band this much louder than the input is ringing on by itself
    static constexpr float kFloorGate = 4.0f;  // notes stand 12 dB over their band's noise floor
    static constexpr float kInputFloorGate = 0.5f; // ... and over half the input's
    static constexpr float kStartFloor = 0.0001f;
    static constexpr double kLearnMs = 250.0;  // the bands' floors are learnt this long after a reset (no notes)
    static constexpr float kLearnLevel = 0.005f; // ... while the input stays under this (-46 dBFS): playing
    static constexpr double kBandFloorFallMs = 300.0;
    // how fast a band's floor rises (dB/s): while nothing sounds, while other notes sound, and while it sounds
    static constexpr double kBandFloorRiseDb[3] = { 20.0, 3.0, 1.0 };
    static constexpr double kLostMs = 40.0;    // a playing note not found keeps sounding this long
    static constexpr float kLostFall = 0.7f;   // ... unless it falls under this share of its level meanwhile
    // Semitones from a note up to its 1st..8th harmonics (the 7th, 33.7, rounds to 34)
    static constexpr int kPartials = 8;
    static constexpr int kPartialSteps[kPartials] = { 0, 12, 19, 24, 28, 31, 34, 36 };
    static constexpr float kFundamental = 0.05f; // a note's own band against its loudest harmonic (-26 dB)
    static constexpr float kOwnShare = 0.3f;     // ... and what the notes found before leave of it
    static constexpr float kOdd = 0.25f;         // its 3rd or 5th against its 2nd (else only a strong fundamental
    static constexpr float kStrongFundamental = 0.5f; // makes it a note: it's below one)
    static constexpr float kLowerShare = 0.3f;   // a note below scoring this share of the best is taken first
    static constexpr float kFocusScale = 0.535f; // Focus 70% (2.3) explains an octave up to 1.23x its odd neighbours
    static constexpr float kPlayingFocus = 0.5f; // ... and half that of a note already playing there

    // Takes out of `rest` what a found note explains: its fundamental, its odd harmonics and the even ones up to
    // Focus times the louder odd neighbour (more is a note an octave up), and what the neighbouring bands let
    // through of them. The note's level is its loudest partial.
    void explain(int b, std::array<float, kMeasured>& rest, const std::array<float, kMeasured>& level)
    {
        std::array<float, kPartials> part {};
        for (int h = 0; h < kPartials; ++h)
        {
            const int m = b + kPartialSteps[h];
            part[h] = m < kMeasured ? rest[m] : 0.0f;
        }
        float loudest = 0.0f;
        for (int h = 0; h < kPartials; ++h)
        {
            const int m = b + kPartialSteps[h];
            if (m >= kMeasured)
                break;
            float take = part[h];
            if ((h & 1) == 1) // 2nd, 4th, 6th, 8th: an octave of a lower partial
            {
                // (a note playing there needs less to stay a note than to start)
                const float odd = 0.5f * (part[h - 1] + (h + 1 < kPartials ? part[h + 1] : part[h - 1]));
                const bool playing = m < kBands && fBands[m].sounding;
                take = std::min(take, (playing ? kPlayingFocus : 1.0f) * kFocusScale * fFocusRatio * odd);
            }
            rest[m] -= take;
            if (m > 0)
                rest[m - 1] = std::max(0.0f, rest[m - 1] - fLeak[m - 1] * take);
            if (m + 1 < kMeasured)
                rest[m + 1] = std::max(0.0f, rest[m + 1] - fLeak[m + 1] * take);
            if (h < 3)
                loudest = std::max(loudest, take);
        }
        fBands[b].level = std::max(level[b], loudest);
    }

    Frame& frame(int64_t hop)
    {
        const auto size = static_cast<int64_t>(fFrames.size());
        return fFrames[static_cast<size_t>(((hop % size) + size) % size)];
    }

    // The band's pitch from how fast its quarter-cycle pair turns. Away from the band's note the pair's sizes differ
    // a little, so the turn rate ripples within each cycle; the smoothing evens that out.
    void measurePitch(Band& band, const Filter& filter, const Svf::Out& out) const
    {
        const double cross = static_cast<double>(filter.lp) * out.band - static_cast<double>(filter.bp) * out.low;
        const double dot = static_cast<double>(filter.lp) * out.low + static_cast<double>(filter.bp) * out.band;
        if (cross == 0.0 && dot == 0.0)
            return;
        const double step = std::fabs(std::atan2(cross, dot)) / (2.0 * M_PI);
        band.measured += (step - band.measured) * fFreqCoef;
        // within a semitone of the band's own note (a stronger neighbour can pull it further)
        const double up = std::exp2(1.0 / 12.0);
        band.measured = std::clamp(band.measured, band.dt / up, band.dt * up);
    }

    // Each band's noise floor: learnt after a reset, then down quickly to the band's level and up slowly (fastest
    // while nothing sounds; slowly under a note, so a steady hum that starts later is found out too)
    void followBandFloors(const std::array<float, kMeasured>& level)
    {
        if (fLearning && (fHopCount >= fLearnHops || fEnvelope > kLearnLevel))
            fLearning = false;
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            if (fLearning)
                band.floor += (level[b] - band.floor) * fLearnCoef;
            else if (level[b] < band.floor)
                band.floor += (level[b] - band.floor) * fBandFloorFall;
            else
                band.floor = std::min(0.03f, band.floor * fBandFloorRise[fSounding == 0 ? 0 : band.sounding ? 2 : 1]);
        }
    }

    // A playing note not found this hop keeps sounding a little longer, so it doesn't flicker; not when its level
    // falls (a mute), a pick comes, or a neighbour is louder or a note starts next to it (a bend, a slide, the next
    // note)
    void holdLostNotes(const std::array<float, kMeasured>& level, int count)
    {
        for (int b = 0; b < kBands; ++b)
        {
            Band& band = fBands[b];
            if (band.on || !band.sounding)
            {
                band.lost = 0;
                continue;
            }
            if (band.lost == 0)
                band.lostLevel = level[b];
            // (a louder neighbour: the note moved there)
            bool moved = fSincePick == 0 || (b > 0 && level[b - 1] > level[b]) || level[b + 1] > level[b];
            for (int d = -2; d <= 2; ++d)
                if (d != 0 && b + d >= 0 && b + d < kBands && fBands[b + d].on && !fBands[b + d].sounding)
                    moved = true;
            if (!moved && count < fVoices && ++band.lost <= fLostHops && !stale(level[b])
                && level[b] >= kLostFall * band.lostLevel && level[b] > gate(b))
            {
                band.on = true;
                ++count;
            }
        }
    }

    // The level a note in band b must be over
    float gate(int b) const { return std::max({ kFloor, kFloorGate * fBands[b].floor, kInputFloorGate * fFloor }); }

    void pickNotes()
    {
        // at the band's note the band-pass and low-pass outputs have equal size and are a quarter cycle apart, so
        // together they give the amplitude without ripple; k makes the peak gain 1
        std::array<float, kMeasured> level {};
        for (int m = 0; m < kMeasured; ++m)
        {
            const Filter& filter = fFilters[m];
            level[m] = filter.stages[1].k() * std::sqrt(filter.bp * filter.bp + filter.lp * filter.lp);
        }
        followBandFloors(level);

        // Find the notes one at a time: each candidate scores the weighted sum of its harmonics' levels; the best
        // is a note, and what it explains (its harmonics, and what of them the neighbouring bands let through) is
        // taken out before the next. A candidate's own band must hold a peak (playing notes count kHold louder),
        // stand over the floors, and not be much weaker than its harmonics (a subharmonic has nothing there).
        const float loudest = *std::max_element(level.begin(), level.end());
        std::array<float, kMeasured> rest = level;
        std::array<bool, kBands> taken {};
        std::array<int, kMaxVoices> chosen {};
        int count = 0;
        float first = 0.0f;
        for (int b = 0; b < kBands; ++b)
            fBands[b].level = level[b];
        while (count < fVoices && !fLearning)
        {
            std::array<float, kBands> scores {};
            int best = -1;
            for (int b = 0; b < kBands; ++b)
            {
                if (taken[b])
                    continue;
                const float hold = fBands[b].on ? kHold : 1.0f;
                const float self = level[b] * (fBands[b].sounding ? kHold : 1.0f);
                // (the lowest band has no neighbour below: it must stand clearly over the one above, or a palm
                // mute's thump is a C2)
                if (!(self > (b > 0 ? level[b - 1] : level[b + 1]) && self >= level[b + 1]) || level[b] * hold <= gate(b)
                    || stale(level[b]) || rest[b] * hold < kOwnShare * level[b])
                    continue;
                float score = 0.0f, strongest = 0.0f, low = 0.0f;
                for (int h = 0; h < kPartials; ++h)
                {
                    const int m = b + kPartialSteps[h];
                    if (m >= kMeasured)
                        break;
                    score += fWeight[b][h] * rest[m];
                    strongest = std::max(strongest, level[m]);
                    if (h < 3)
                        low = std::max(low, rest[m]);
                }
                // its first three partials within reach of the loudest band
                if (low * hold < kRelativeLevel * loudest)
                    continue;
                if (level[b] < kFundamental * strongest)
                    continue;
                // a note below one played (or under a muted strum's thump) has only even harmonics there: a real one
                // has its 3rd or 5th, or a strong fundamental of its own
                // (of what the notes found so far don't explain)
                const float even = rest[b + 12];
                const float odd = std::max(b + 19 < kMeasured ? rest[b + 19] : 0.0f,
                                           b + 28 < kMeasured ? rest[b + 28] : 0.0f);
                if (b + 19 < kMeasured && odd < kOdd * even && level[b] < kStrongFundamental * even)
                    continue;
                scores[b] = score * hold;
                if (best < 0 || scores[b] > scores[best])
                    best = b;
            }
            // a note an octave, a twelfth or two octaves below that is there too is the one played: the best one is
            // its harmonic (the low strings' fundamentals are weak, especially from a bridge pickup)
            for (bool lower = best >= 0; lower;)
            {
                lower = false;
                for (const int d : { 12, 19, 24 })
                    if (best - d >= 0 && scores[best - d] >= kLowerShare * scores[best])
                    {
                        best -= d;
                        lower = true;
                        break;
                    }
            }
            const float bestScore = best >= 0 ? scores[best] : 0.0f;
            if (best < 0 || bestScore < kRelative * first)
                break;
            first = std::max(first, bestScore);
            taken[best] = true;
            chosen[count++] = best;
            explain(best, rest, level);
        }
        for (Band& band : fBands)
            band.on = false;
        for (int v = 0; v < count; ++v)
            fBands[chosen[v]].on = true;
        holdLostNotes(level, count);

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
    std::array<Filter, kMeasured> fFilters;
    std::array<float, kMeasured> fLeak {};
    std::array<std::array<float, kPartials>, kBands> fWeight {};
    std::array<Band, kBands> fBands;
    std::array<int, kBands> fRingUpHops {};
    std::vector<Frame> fFrames;
    int64_t fHopCount = 0;

    bool stale(float level) const { return level > kStale * fEnvelope; }

    OnsetDetector fOnset, fClick;
    Biquad fClickFilter;
    bool fPickPending = false;
    int fSincePick = 1 << 20;
    int64_t fPickHop = 0;
    int fRepickHops = 22, fMutedHops = 45;
    int fLearnHops = 375, fLostHops = 60;
    bool fLearning = true;
    float fLearnCoef = 0.02f, fBandFloorFall = 0.002f;
    std::array<float, 3> fBandFloorRise { 1.0f, 1.0f, 1.0f };
    float fEnvelope = 0.0f, fFloor = 0.0003f;
    float fEnvAttack = 1.0f, fEnvRelease = 0.99f, fFloorFall = 0.001f, fFloorRise = 1.0f;
    int fHold = 0, fEnvHold = 800;
    int fSounding = 0;
    int fPicks = 0;
};

} // namespace namsayin
