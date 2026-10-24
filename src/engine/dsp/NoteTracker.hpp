// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/OnsetDetector.hpp"
#include "engine/dsp/PitchTracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace namsayin {

// Turns the pitch tracker's estimates into notes for the synths and pitch pedals: when a note starts, at which
// pitch, and when it ends.
// - The level is a peak follower that holds for a period and then falls fast, so a muted string ends its note
//   within a few ms (the tracker's own follower releases over 80 ms, so notes ran on past every mute).
// - A note starts at a pitch two estimates in a row agree on (about 4 ms apart), with a clear pitch; once it plays,
//   less clear estimates near its pitch still follow it (bends, vibrato, the decay). It ends when the level falls
//   back to the noise floor (followed, so hum doesn't hold notes on) or well below the note's peak, or when no
//   clear pitch has been found for 35 ms (palm mutes, scrapes).
// - A new pick (OnsetDetector) ends the note before it, so the old pitch doesn't run into the new note; a pitch
//   change without a pick (hammer-ons, slides) moves the note once its new pitch is confirmed.
// - The Pick Filter: a new note must also hold its pitch for that long before it sounds.
// - Lookahead (optional, up to kMaxLookaheadMs): the output is that much behind the input, and the block delays
//   its audio to match (reporting the latency). Each note then starts back at its pick, at the pitch found
//   after it, so notes start on time without the pick's unsteady pitch, and pick-filter waits cost nothing.
class NoteTracker {
public:
    static constexpr double kMaxLookaheadMs = 30.0;

    struct Note {
        bool on = false;
        double hz = 0.0;       // the pitch; kept after the note ends (0 until the first note)
        float envelope = 0.0f; // the input's level
        bool onset = false;    // a new pick starts a note at this sample
        float velocity = 0.0f; // how hard the note was picked: the input's peak from its pick until it was found
    };

    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fTracker.prepare(sampleRate);
        fOnsetDetector.prepare(sampleRate);
        fRing.assign(static_cast<size_t>(std::ceil(kMaxLookaheadMs / 1000.0 * sampleRate)) + 2, Note {});
        fAttack = 1.0f - EnvelopeFollower::coef(sampleRate, 0.3);
        fRelease = EnvelopeFollower::coef(sampleRate, 4.0);
        fRecentRelease = EnvelopeFollower::coef(sampleRate, 20.0);
        fFloorFall = 1.0f - EnvelopeFollower::coef(sampleRate, 20.0);
        fFloorRise = static_cast<float>(std::pow(10.0, 6.0 / 20.0 / sampleRate)); // 6 dB/s
        fUnvoicedLimit = static_cast<int64_t>(0.035 * sampleRate);
        fClickFilter.setHighPass(sampleRate, 2000.0);
        fClickDetector.prepare(sampleRate);
        fClickDetector.setThreshold(0.002f);
        fDefaultHold = static_cast<int>(0.015 * sampleRate);
        setPickFilter(fPickMs);
        setLookahead(fLookaheadMs);
        reset();
    }

    // Non-RT (it changes the latency). Clamped to kMaxLookaheadMs.
    void setLookahead(double ms)
    {
        fLookaheadMs = std::clamp(ms, 0.0, kMaxLookaheadMs);
        fLookahead = static_cast<int>(std::lround(fLookaheadMs / 1000.0 * fSampleRate));
        if (!fRing.empty())
            fLookahead = std::min<int>(fLookahead, static_cast<int>(fRing.size()) - 2);
    }

    // The latency the output has, in samples
    uint32_t latency() const { return static_cast<uint32_t>(fLookahead); }

    void setPickFilter(float ms)
    {
        fPickMs = ms;
        fPickHold = static_cast<int64_t>(std::lround(ms * 0.001 * fSampleRate));
    }

    void reset()
    {
        fTracker.reset();
        fOnsetDetector.reset();
        fClickFilter.reset();
        fClickDetector.reset();
        fMisses = 0;
        fEstimates = 0;
        std::fill(fRing.begin(), fRing.end(), Note {});
        fTime = 0;
        fEnvelope = 0.0f;
        fHold = 0;
        fFloor = 0.0003f;
        fOn = false;
        fSounding = false;
        fHz = 0.0;
        fPeak = 0.0f;
        fRecent = 0.0f;
        fMuted = false;
        fCandidate = Candidate {};
        fPickPending = false;
        fPickPeak = fVelocity = 0.0f;
        fPickTime = 0;
        fLastVoiced = 0;
        fOut = Note {};
    }

    // One input sample (the signal to track); returns the note at the output (fLookahead samples ago)
    const Note& process(float in)
    {
        ++fTime;
        followLevel(in);
        // a pick: a jump in the level, or in the level of the pick's click (high-passed), which stands out even
        // while a loud note rings
        bool picked = fOnsetDetector.process(std::fabs(in));
        picked = fClickDetector.process(std::fabs(fClickFilter.process(in))) || picked;
        fTracker.process(in);
        const bool estimated = fTracker.estimates() != fEstimates;
        fEstimates = fTracker.estimates();

        Note& now = at(fTime);
        now = Note { fOn, fHz, fEnvelope, false, fVelocity };
        if (fPickPending)
            fPickPeak = std::max(fPickPeak, fEnvelope);

        if (picked)
        {
            fPickPending = true;
            fPickTime = fTime;
            fPickPeak = fEnvelope;
            if (fOn)
                end(fTime, EndReason::Pick);
            fCandidate = Candidate {};
            fMuted = false;
        }
        if (fOn && fEnvelope < kMuteDrop * fRecent)
        {
            // falling far faster than a ringing string does: muted. Its tail mustn't start a note again.
            end(fTime, EndReason::Mute);
            fMuted = true;
        }
        else if (fMuted && fEnvelope > 0.5f * fRecent)
        {
            fMuted = false; // swelling back (a volume swell, a note from another string)
        }
        if (!fSounding)
        {
            if (fOn)
                end(fTime, EndReason::Gate);
            fCandidate = Candidate {};
            fPickPending = false;
            fMuted = false;
        }
        else if (estimated)
        {
            estimate(fTracker.voiced() ? fTracker.hz() : 0.0, fTracker.aperiodicity());
        }

        fOut = at(fTime - fLookahead);
        if (fLookahead == 0)
            fOut = Note { fOn, fHz, fEnvelope, now.onset, fVelocity };
        if (fOut.hz <= 0.0)
            fOut.hz = fHz;
        return fOut;
    }

    const Note& note() const { return fOut; }

    // Why the last note ended (for tests)
    enum class EndReason { None, Pick, Mute, Gate, Unclear, OtherPitch };
    EndReason endReason() const { return fEndReason; }

private:
    struct Candidate {
        double hz = 0.0;
        int count = 0;
        int64_t start = 0;
    };

    static constexpr float kClear = 0.3f;     // aperiodicity a new note needs
    static constexpr float kMinGate = 0.0015f; // about -56 dBFS
    static constexpr float kFloorGate = 4.0f;  // a note starts 12 dB over the noise floor
    static constexpr float kPeakGate = 0.003f; // and ends 50 dB under its peak
    static constexpr float kMuteDrop = 0.1f;   // or 20 dB under the recent level (a mute)
    static constexpr int kMaxMisses = 2;       // estimates in a row that don't find the note end it

    Note& at(int64_t time)
    {
        const auto size = static_cast<int64_t>(fRing.size());
        return fRing[static_cast<size_t>(((time % size) + size) % size)]; // (time is negative at the start)
    }

    static double semitones(double a, double b) { return std::fabs(12.0 * std::log2(a / b)); }

    void followLevel(float in)
    {
        const float a = std::fabs(in);
        if (a > fEnvelope)
        {
            fEnvelope += (a - fEnvelope) * fAttack;
            // hold for a period of the note (or 15 ms), so the level doesn't ripple between the wave's peaks
            fHold = fOn && fHz > 0.0 ? static_cast<int>(1.2 * fSampleRate / fHz) : fDefaultHold;
        }
        else if (fHold > 0)
        {
            --fHold;
        }
        else
        {
            fEnvelope *= fRelease;
        }
        // the recent level: a slower follower, which a muted string's level falls far below
        fRecent = fEnvelope > fRecent ? fEnvelope : fRecent * fRecentRelease;
        // the noise floor: follows the level down quickly, and up only slowly, between notes
        if (fEnvelope < fFloor)
            fFloor += (fEnvelope - fFloor) * fFloorFall;
        else if (!fOn)
            fFloor = std::min(0.03f, fFloor * fFloorRise);

        const float open = std::max(kMinGate, kFloorGate * fFloor);
        if (fOn)
            fPeak = std::max(fPeak, fEnvelope);
        if (!fSounding)
            fSounding = fEnvelope > open;
        else if (fEnvelope < std::max(0.5f * open, fOn ? kPeakGate * fPeak : 0.0f))
            fSounding = false;
    }

    // One estimate: a pitch, or 0 for none
    void estimate(double hz, float aperiodicity)
    {
        if (fOn && hz > 0.0 && semitones(hz, fHz) < 0.6)
        {
            // the playing note: follow it (bends, vibrato), even when it's less clear
            fHz = hz;
            fLastVoiced = fTime;
            fMisses = 0;
            fCandidate = Candidate {};
            return;
        }
        if (hz <= 0.0 || aperiodicity > kClear)
        {
            // no clear pitch (a chord, a scrape, the decay): the note plays on, for a while
            if (fOn && fTime - fLastVoiced > fUnvoicedLimit)
                end(std::max(fTime - fLookahead, fLastVoiced), EndReason::Unclear);
            return;
        }
        // a clear, different pitch: two in a row end the note (back to the change)
        if (fOn && ++fMisses == 1)
            fFirstMiss = fTime;
        if (fOn && fMisses >= kMaxMisses)
            end(fFirstMiss - static_cast<int64_t>(0.004 * fSampleRate), EndReason::OtherPitch);
        if (fMuted)
            return;
        if (fCandidate.count > 0 && semitones(hz, fCandidate.hz) < 0.5)
        {
            fCandidate.hz = hz;
            ++fCandidate.count;
        }
        else
        {
            fCandidate = Candidate { hz, 1, fTime };
        }
        if (fCandidate.count >= 2 && fTime - fCandidate.start >= fPickHold)
            start();
    }

    // The candidate becomes the note
    void start()
    {
        // where the note began: at its pick, else about half an analysis window before its first estimate
        int64_t begin = fCandidate.start - static_cast<int64_t>(0.008 * fSampleRate);
        if (fPickPending)
            begin = fPickTime;
        begin = std::clamp<int64_t>(begin, fTime - fLookahead, fTime);
        fOn = true;
        fMisses = 0;
        fHz = fCandidate.hz;
        fPeak = fEnvelope;
        fVelocity = fPickPending ? std::max(fPickPeak, fEnvelope) : fEnvelope;
        fLastVoiced = fTime;
        for (int64_t t = begin; t <= fTime; ++t)
        {
            Note& n = at(t);
            n.on = true;
            n.hz = fHz;
            n.onset = false;
            n.velocity = fVelocity;
        }
        at(begin).onset = fPickPending;
        fPickPending = false;
        fCandidate = Candidate {};
    }

    // Ends the note from `from` (back to at most the lookahead)
    void end(int64_t from, EndReason reason)
    {
        fOn = false;
        fEndReason = reason;
        for (int64_t t = std::max(from, fTime - fLookahead + 1); t <= fTime; ++t)
            at(t).on = false;
    }

    double fSampleRate = 48000.0;
    PitchTracker fTracker;
    OnsetDetector fOnsetDetector;
    Biquad fClickFilter;
    OnsetDetector fClickDetector;
    std::vector<Note> fRing;
    int fLookahead = 0;
    double fLookaheadMs = 0.0;
    float fPickMs = 0.0f;
    int64_t fPickHold = 0;
    int64_t fTime = 0;

    float fAttack = 1.0f, fRelease = 0.99f, fFloorFall = 0.001f, fFloorRise = 1.0f;
    float fRecentRelease = 0.999f;
    float fEnvelope = 0.0f, fFloor = 0.0003f, fPeak = 0.0f, fRecent = 0.0f;
    bool fMuted = false;
    int fHold = 0, fDefaultHold = 720;
    bool fSounding = false;

    bool fOn = false;
    double fHz = 0.0;
    Candidate fCandidate;
    bool fPickPending = false;
    float fPickPeak = 0.0f, fVelocity = 0.0f;
    int64_t fPickTime = 0, fLastVoiced = 0;
    int64_t fUnvoicedLimit = 1680;
    int fMisses = 0;
    int64_t fFirstMiss = 0;
    uint32_t fEstimates = 0;
    Note fOut;
    EndReason fEndReason = EndReason::None;
};

} // namespace namsayin
