// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

// Real-time monophonic pitch tracking for the synth and the harmonizer: YIN (de Cheveigné and Kawahara, 2002) on a
// copy decimated to about 12 kHz, every 4 ms, over a 17 ms window, for 60..1500 Hz; the period is then refined at
// the full rate (difference function around it, parabolic peak) to within a few cents. YIN's cumulative-mean
// normalized difference and absolute threshold keep it on the fundamental of a plucked string, whose harmonics are
// often stronger. A note is reported from about 20 ms after it starts; no latency is added to the audio. Chords
// confuse it (it reports no pitch, or one of the notes).
class PitchTracker {
public:
    static constexpr double kMinHz = 60.0;
    static constexpr double kMaxHz = 1500.0;

    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fDecimation = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.0)));
        fRate = sampleRate / fDecimation;
        fMinLag = std::max(2, static_cast<int>(fRate / kMaxHz));
        fMaxLag = std::min(kMaxLags - 2, static_cast<int>(std::ceil(fRate / kMinHz)));
        fWindow = fMaxLag;
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
    float envelope() const { return fEnvelope.value(); }
    bool sounding() const { return fEnvelope.value() > kGateLevel; }

private:
    static constexpr int kHistory = 512;
    static constexpr int kFullHistory = 16384; // full-rate history for the refinement (enough for 60 Hz at 192 kHz)
    static constexpr int kMaxLags = 256;
    static constexpr float kThreshold = 0.15f;  // YIN's absolute threshold
    static constexpr float kVoicedLimit = 0.35f; // above this at the best lag: no clear pitch
    static constexpr float kGateLevel = 0.002f;

    float at(int age) const { return fHistory[static_cast<size_t>((fWrite - 1 - age + 2 * kHistory) % kHistory)]; }
    float fullAt(int age) const
    {
        return fFull[static_cast<size_t>((fFullWrite - 1 - age + 2 * kFullHistory) % kFullHistory)];
    }

    // The period at the full rate: the plain difference function over two periods, at lags around the decimated
    // estimate, with a parabolic peak
    double refine(double coarseLag) const
    {
        const double centre = coarseLag * fDecimation;
        const int from = std::max(2, static_cast<int>(std::floor(centre)) - fDecimation - 1);
        const int to = static_cast<int>(std::ceil(centre)) + fDecimation + 1;
        const int window = std::min(2 * to, kFullHistory - to - 2);
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
        // difference function over the newest fWindow samples
        std::array<float, kMaxLags> d {};
        for (int lag = 1; lag <= fMaxLag; ++lag)
        {
            float sum = 0.0f;
            for (int j = 0; j < fWindow; ++j)
            {
                const float diff = at(j) - at(j + lag);
                sum += diff * diff;
            }
            d[static_cast<size_t>(lag)] = sum;
        }
        // cumulative mean normalized difference
        float running = 0.0f;
        d[0] = 1.0f;
        for (int lag = 1; lag <= fMaxLag; ++lag)
        {
            running += d[static_cast<size_t>(lag)];
            d[static_cast<size_t>(lag)] = running > 0.0f ? d[static_cast<size_t>(lag)] * lag / running : 1.0f;
        }
        // the first dip under the threshold (then down to its bottom); else the deepest dip
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
        if (best < 0)
        {
            best = fMinLag;
            for (int lag = fMinLag; lag < fMaxLag; ++lag)
                if (d[static_cast<size_t>(lag)] < d[static_cast<size_t>(best)])
                    best = lag;
        }
        fVoiced = d[static_cast<size_t>(best)] < kVoicedLimit;
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
        fHz = fSampleRate / refine(lag);
        return true;
    }

    double fSampleRate = 48000.0;
    double fRate = 12000.0;
    int fDecimation = 4;
    int fMinLag = 8, fMaxLag = 200, fWindow = 200, fHop = 48;
    Biquad fHighPass;
    Biquad fAntiAlias[2];
    EnvelopeFollower fEnvelope;
    std::array<float, kHistory> fHistory {};
    int fWrite = 0;
    std::array<float, kFullHistory> fFull {};
    int fFullWrite = 0;
    int fPhase = 0;
    int fSinceHop = 0;
    double fHz = 0.0;
    bool fVoiced = false;
};

} // namespace namsayin
