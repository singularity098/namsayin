// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace namsayin {

// Real-time monophonic pitch tracking for the synths and pitch pedals (dsp/NoteTracker.hpp turns it into notes):
// YIN (de Cheveigné and Kawahara, 2002) on a copy decimated to about 12 kHz, every 4 ms, for 28..1500 Hz (a 5-string
// bass's low B, 31 Hz, up; owner, 2026-10-29: was 60 Hz, which missed a 7-string's drop A at 55 Hz); the period
// is then refined at the full rate (difference function around it, parabolic peak) to within a few cents. The
// difference at each lag is over a window of one lag (at least 2 ms), so a note's estimate needs about two of its
// periods, and is relative to the windows' energy, so fast swells and decays don't blur it. YIN's cumulative-mean
// normalized difference and absolute threshold keep it on the fundamental of a plucked string, whose harmonics are
// often stronger. A note is reported from about 10 ms after it starts; no latency is added to the audio. Chords
// confuse it (it reports no pitch, or one of the notes); a chord whose notes repeat together an octave below its root
// (power chords, major chords) reports the root, not that octave. Under 60 Hz a pitch also needs its fundamental
// within 9 dB of its next harmonics: strings left ringing together repeat at their notes' lowest common period, often
// down there, with nothing at it (a 7-string's or a bass's low notes have theirs).
class PitchTracker {
public:
    static constexpr double kMinHz = 28.0;
    static constexpr double kMaxHz = 1500.0;

    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fDecimation = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.0)));
        fRate = sampleRate / fDecimation;
        fMinLag = std::max(2, static_cast<int>(fRate / kMaxHz));
        fMaxLag = std::min(kMaxLags - 2, static_cast<int>(std::ceil(fRate / kMinHz)));
        fLowLag = std::min(fMaxLag, static_cast<int>(std::ceil(fRate / kLowHz)));
        fWindow = std::max(4, static_cast<int>(0.002 * fRate));
        fHop = std::max(1, static_cast<int>(0.004 * fRate));
        fHighPass.setHighPass(sampleRate, 50.0);
        for (Biquad& filter : fAntiAlias)
            filter.setLowPass(sampleRate, std::min(0.38 * fRate, 0.45 * sampleRate));
        fEnvelope.prepare(sampleRate, 1.0, 80.0);
        reset();
    }

    void reset()
    {
        fHighPass.reset();
        for (Biquad& filter : fAntiAlias)
            filter.reset();
        fEnvelope.reset();
        fHistory.fill(0.0f);
        fWrite = 0;
        fFull.fill(0.0f);
        fFullWrite = 0;
        fPhase = 0;
        fSinceHop = 0;
        fHz = 0.0;
        fAperiodicity = 1.0f;
        fVoiced = false;
    }

    // One input sample. Returns true when a pitch estimate was made and accepted.
    bool process(float in)
    {
        const float x = fHighPass.process(in);
        fEnvelope.process(x);
        fFull[static_cast<size_t>(fFullWrite)] = x;
        fFullWrite = (fFullWrite + 1) % kFullHistory;
        const float filtered = fAntiAlias[1].process(fAntiAlias[0].process(x));
        if (++fPhase < fDecimation)
            return false;
        fPhase = 0;
        fHistory[static_cast<size_t>(fWrite)] = filtered;
        fWrite = (fWrite + 1) % kHistory;
        if (++fSinceHop < fHop)
            return false;
        fSinceHop = 0;
        if (!sounding())
        {
            fVoiced = false;
            return false;
        }
        return estimate();
    }

    double hz() const { return fHz; } // the last accepted pitch; 0 until one is found
    bool voiced() const { return fVoiced; } // the last estimate found a clear pitch
    uint32_t estimates() const { return fEstimates; } // counts every estimate made, with or without a clear pitch
    // How aperiodic the last estimate's signal was: YIN's normalized difference at the chosen period (0 = a pure
    // tone, about 0.1 for a plucked string, over kVoicedLimit for noise)
    float aperiodicity() const { return fAperiodicity; }
    float envelope() const { return fEnvelope.value(); }
    bool sounding() const { return fEnvelope.value() > kGateLevel; }

private:
    static constexpr int kHistory = 1024;
    static constexpr int kFullHistory = 16384; // full-rate history for the refinement (enough for 28 Hz at 192 kHz)
    static constexpr int kMaxLags = 512;
    static constexpr float kThreshold = 0.15f;  // YIN's absolute threshold
    static constexpr float kVoicedLimit = 0.35f; // above this at the best lag: no clear pitch
    static constexpr float kGateLevel = 0.002f;
    static constexpr double kSubCheckHz = 200.0; // pitches under this are checked for a chord's octave below
    static constexpr float kSubBoth = 0.2f;        // ... its 2nd and 3rd harmonics within 14 dB of each other
    static constexpr float kSubFundamental = 0.05f; // ... and its 1st 26 dB under them
    static constexpr double kLowHz = 60.0;          // a pitch under this needs its fundamental there:
    static constexpr float kLowFundamental = 0.35f; // within 9 dB of its 2nd to 4th harmonics

    float at(int age) const { return fHistory[static_cast<size_t>((fWrite - 1 - age + 2 * kHistory) % kHistory)]; }
    float fullAt(int age) const
    {
        return fFull[static_cast<size_t>((fFullWrite - 1 - age + 2 * kFullHistory) % kFullHistory)];
    }

    // The sum of a[j] * b[j] for j < n, in eight running sums, so the compiler can vectorize it
    static float dot(const float* a, const float* b, int n)
    {
        float sums[8] = {};
        int j = 0;
        for (; j + 8 <= n; j += 8)
            for (int k = 0; k < 8; ++k)
                sums[k] += a[j + k] * b[j + k];
        float sum = ((sums[0] + sums[1]) + (sums[2] + sums[3])) + ((sums[4] + sums[5]) + (sums[6] + sums[7]));
        for (; j < n; ++j)
            sum += a[j] * b[j];
        return sum;
    }

    // The period at the full rate: the plain difference function over two periods, at lags around the decimated
    // estimate, with a parabolic peak
    double refine(double coarseLag) const
    {
        const double centre = coarseLag * fDecimation;
        const int from = std::max(2, static_cast<int>(std::floor(centre)) - fDecimation - 1);
        const int to = static_cast<int>(std::ceil(centre)) + fDecimation + 1;
        const int window = std::min(to, kFullHistory - to - 2);
        if (fDecimation == 1 || window < to)
            return centre;
        float values[64];
        const int count = std::min(64, to - from + 1);
        int best = 0;
        for (int k = 0; k < count; ++k)
        {
            const int lag = from + k;
            float sum = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float diff = fullAt(j) - fullAt(j + lag);
                sum += diff * diff;
            }
            values[k] = sum;
            if (sum < values[best])
                best = k;
        }
        double lag = from + best;
        if (best > 0 && best < count - 1)
        {
            const float a = values[best - 1], b = values[best], c = values[best + 1];
            const float denom = a - 2.0f * b + c;
            if (denom > 0.0f)
                lag += std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
        }
        return lag;
    }

    bool estimate()
    {
        // difference function over the newest samples: a window of one lag at each lag (at least fWindow), so a
        // note's estimate needs only about two of its periods, not a fixed window plus a period. The samples are
        // copied out newest first, so the products run over plain arrays, and the windows' energies come from
        // running sums of the squares.
        const int span = fMaxLag + std::max(fWindow, fMaxLag);
        fSquares[0] = 0.0;
        for (int j = 0; j < span; ++j)
        {
            const float x = at(j);
            fRecent[static_cast<size_t>(j)] = x;
            fSquares[static_cast<size_t>(j + 1)] = fSquares[static_cast<size_t>(j)] + static_cast<double>(x) * x;
        }
        std::array<float, kMaxLags>& d = fDiff;
        for (int lag = 1; lag <= fMaxLag; ++lag)
        {
            const int window = std::max(fWindow, lag);
            const float cross = dot(fRecent.data(), fRecent.data() + lag, window);
            const double now = fSquares[static_cast<size_t>(window)];
            const double before = fSquares[static_cast<size_t>(lag + window)] - fSquares[static_cast<size_t>(lag)];
            // the difference relative to the two windows' energy: a note that swells or dies away fast (a pick
            // attack, a palm mute) differs from itself a period ago as little as a steady one
            const auto energy = static_cast<float>(now + before);
            d[static_cast<size_t>(lag)] = energy > 1e-20f ? (energy - 2.0f * cross) / energy : 1.0f;
        }
        // cumulative mean normalized difference
        float running = 0.0f;
        d[0] = 1.0f;
        for (int lag = 1; lag <= fMaxLag; ++lag)
        {
            running += d[static_cast<size_t>(lag)];
            d[static_cast<size_t>(lag)] = running > 0.0f ? d[static_cast<size_t>(lag)] * lag / running : 1.0f;
        }
        // the first dip under the threshold (then down to its bottom); else the deepest dip. A dip under kLowHz
        // (a bass, a 7-string's drop A) only with its fundamental there: strings left ringing together repeat at
        // the lowest common period of their notes, which is often down there, with nothing at it.
        int best = -1;
        for (int lag = fMinLag; lag < fMaxLag; ++lag)
        {
            if (d[static_cast<size_t>(lag)] < kThreshold)
            {
                while (lag + 1 < fMaxLag && d[static_cast<size_t>(lag + 1)] < d[static_cast<size_t>(lag)])
                    ++lag;
                best = lag;
                break;
            }
        }
        if (best >= fLowLag && !fundamentalThere(best))
            best = -1;
        if (best < 0)
        {
            best = fMinLag;
            for (int lag = fMinLag; lag < fLowLag; ++lag)
                if (d[static_cast<size_t>(lag)] < d[static_cast<size_t>(best)])
                    best = lag;
        }
        ++fEstimates;
        fAperiodicity = d[static_cast<size_t>(best)];
        fVoiced = fAperiodicity < kVoicedLimit;
        if (!fVoiced)
            return false;

        // parabolic refinement
        double lag = best;
        if (best > fMinLag && best < fMaxLag - 1)
        {
            const float a = d[static_cast<size_t>(best - 1)], b = d[static_cast<size_t>(best)],
                        c = d[static_cast<size_t>(best + 1)];
            const float denom = a - 2.0f * b + c;
            if (denom > 0.0f)
                lag += std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
        }
        // a chord's notes (a power chord's root and fifth, an open chord's root, fifth and third) repeat together
        // every two root periods, an octave below the root, with nothing there: then it's the root (owner's
        // recordings: B1 under a B5, D2 under an open D)
        if (fSampleRate / (lag * fDecimation) < kSubCheckHz && 2 * lag + 2 < kHistory && octaveBelowChord(lag))
            lag *= 0.5;
        fHz = fSampleRate / refine(lag);
        return true;
    }

    // The levels of the first `count` harmonics of a period of `lag` samples, over the last two periods (Hann window,
    // so the harmonics land on whole bins without leaking into each other: harmonic h is bin 2h). The sines and
    // cosines are rotated sample by sample.
    template <int count>
    std::array<float, count> harmonics(double lag) const
    {
        const int n = static_cast<int>(std::lround(2.0 * lag));
        std::array<double, count> re {}, im {}, c {}, sn {}, rc {}, rs {};
        for (int h = 0; h < count; ++h)
        {
            c[h] = 1.0;
            rc[h] = std::cos(2.0 * M_PI * 2 * (h + 1) / n);
            rs[h] = std::sin(2.0 * M_PI * 2 * (h + 1) / n);
        }
        double wc = 1.0, ws = 0.0; // the window's cosine
        const double wrc = std::cos(2.0 * M_PI / n), wrs = std::sin(2.0 * M_PI / n);
        for (int j = 0; j < n; ++j)
        {
            const double v = (0.5 - 0.5 * wc) * at(n - 1 - j);
            for (int h = 0; h < count; ++h)
            {
                re[h] += v * c[h];
                im[h] -= v * sn[h];
                const double next = c[h] * rc[h] - sn[h] * rs[h];
                sn[h] = sn[h] * rc[h] + c[h] * rs[h];
                c[h] = next;
            }
            const double next = wc * wrc - ws * wrs;
            ws = ws * wrc + wc * wrs;
            wc = next;
        }
        std::array<float, count> mag {};
        for (int h = 0; h < count; ++h)
            mag[h] = static_cast<float>(std::sqrt(re[h] * re[h] + im[h] * im[h]));
        return mag;
    }

    // The 2nd and 3rd harmonics both strong and the 1st nearly absent. A low string's own fundamental can be weak too,
    // but rarely this weak (a synthetic pluck in PedalTests has its 23-26 dB under; on the owner's recordings 20 dB
    // took a few more of the chords' octave-below notes away than 26 dB does).
    bool octaveBelowChord(double lag) const
    {
        const std::array<float, 3> mag = harmonics<3>(lag);
        const float strong = std::max(mag[1], mag[2]);
        return std::min(mag[1], mag[2]) > kSubBoth * strong && mag[0] < kSubFundamental * strong;
    }

    // A low note's fundamental: not far under its next three harmonics
    bool fundamentalThere(int lag) const
    {
        const std::array<float, 4> mag = harmonics<4>(lag);
        return mag[0] >= kLowFundamental * *std::max_element(mag.begin(), mag.end());
    }

    double fSampleRate = 48000.0;
    double fRate = 12000.0;
    int fDecimation = 4;
    int fMinLag = 8, fMaxLag = 200, fLowLag = 200, fWindow = 200, fHop = 48;
    Biquad fHighPass;
    Biquad fAntiAlias[2];
    EnvelopeFollower fEnvelope;
    std::array<float, kHistory> fHistory {};
    int fWrite = 0;
    std::array<float, kFullHistory> fFull {};
    std::array<float, kHistory> fRecent {};     // estimate()'s scratch: the newest samples, newest first,
    std::array<double, kHistory + 1> fSquares {}; // the running sum of their squares,
    std::array<float, kMaxLags> fDiff {};         // and the difference function
    int fFullWrite = 0;
    int fPhase = 0;
    int fSinceHop = 0;
    double fHz = 0.0;
    float fAperiodicity = 1.0f;
    bool fVoiced = false;
    uint32_t fEstimates = 0;
};

} // namespace namsayin
