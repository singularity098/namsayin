// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

// Finds the signal's repetition period by autocorrelation, for chords as well as single notes (a chord repeats,
// near enough, after a few periods of its root: a power chord after two, a major triad after four). Runs on a
// copy decimated to a quarter of the sample rate, every few milliseconds, over lags of 7..60 ms; the peak is
// refined by parabolic interpolation. The pitch shifter uses it to make chord splices line up.
class PeriodFinder {
public:
    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fDecimation = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.0)));
        const double rate = sampleRate / fDecimation;
        fMinLag = static_cast<int>(kMinLagMs / 1000.0 * rate);
        fMaxLag = std::min(kHistory - kSegment - 2, static_cast<int>(kMaxLagMs / 1000.0 * rate));
        // below the decimated rate's Nyquist
        for (Biquad& filter : fAntiAlias)
            filter.setLowPass(sampleRate, std::min(0.38 * rate, 0.45 * sampleRate));
        fInterval = static_cast<int>(kIntervalMs / 1000.0 * rate);
        reset();
    }

    void reset()
    {
        fHistory.fill(0.0f);
        fWrite = 0;
        fPhase = 0;
        fSinceUpdate = 0;
        fPeriod = 0.0;
        fConfidence = 0.0f;
        for (Biquad& filter : fAntiAlias)
            filter.reset();
    }

    // One input sample (mono). Cheap except every few milliseconds, when it searches.
    void process(float x)
    {
        const float filtered = fAntiAlias[1].process(fAntiAlias[0].process(x));
        if (++fPhase < fDecimation)
            return;
        fPhase = 0;
        fHistory[static_cast<size_t>(fWrite)] = filtered;
        fWrite = (fWrite + 1) % kHistory;
        if (++fSinceUpdate >= fInterval)
        {
            fSinceUpdate = 0;
            search();
        }
    }

    // The best period in input samples, or 0 when nothing repeats clearly
    double period() const { return fConfidence >= kMinConfidence ? fPeriod : 0.0; }
    float confidence() const { return fConfidence; }

private:
    static constexpr int kHistory = 1024;
    static constexpr int kSegment = 160;   // compared stretch, decimated samples (~13 ms)
    static constexpr double kMinLagMs = 7.0;
    static constexpr double kMaxLagMs = 60.0;
    static constexpr double kIntervalMs = 8.0;
    static constexpr float kMinConfidence = 0.7f;

    float at(int age) const { return fHistory[static_cast<size_t>((fWrite - 1 - age + 2 * kHistory) % kHistory)]; }

    void search()
    {
        // normalized cross-correlation of the newest segment with the segment `lag` earlier
        float energyNew = 0.0f;
        for (int i = 0; i < kSegment; ++i)
            energyNew += at(i) * at(i);
        if (energyNew < 1e-7f)
        {
            fConfidence = 0.0f;
            return;
        }
        float energyOld = 0.0f;
        for (int i = 0; i < kSegment; ++i)
            energyOld += at(fMinLag - 1 + i) * at(fMinLag - 1 + i);

        std::array<float, kHistory> scores {};
        int best = -1;
        float bestScore = 0.0f;
        for (int lag = fMinLag; lag <= fMaxLag; ++lag)
        {
            // slide the old segment's energy along
            energyOld += at(lag + kSegment - 1) * at(lag + kSegment - 1) - at(lag - 1) * at(lag - 1);
            float dot = 0.0f;
            for (int i = 0; i < kSegment; ++i)
                dot += at(i) * at(i + lag);
            const float score = dot / std::sqrt(std::max(1e-12f, energyNew * energyOld));
            scores[static_cast<size_t>(lag)] = score;
            if (score > bestScore)
            {
                bestScore = score;
                best = lag;
            }
        }
        // prefer the shortest lag that's nearly as good (the period, not a multiple of it)
        for (int lag = fMinLag + 1; lag < best; ++lag)
        {
            const float s = scores[static_cast<size_t>(lag)];
            if (s > 0.995f * bestScore && s >= scores[static_cast<size_t>(lag - 1)]
                && s >= scores[static_cast<size_t>(lag + 1)])
            {
                best = lag;
                break;
            }
        }
        fConfidence = best > 0 ? scores[static_cast<size_t>(best)] : 0.0f;
        if (best <= fMinLag || best >= fMaxLag)
        {
            fPeriod = best > 0 ? best * fDecimation : 0.0;
            return;
        }
        // parabolic peak
        const float a = scores[static_cast<size_t>(best - 1)];
        const float b = scores[static_cast<size_t>(best)];
        const float c = scores[static_cast<size_t>(best + 1)];
        const float denom = a - 2.0f * b + c;
        const double offset = denom < 0.0f ? 0.5 * (a - c) / denom : 0.0;
        fPeriod = (best + std::clamp(offset, -0.5, 0.5)) * fDecimation;
    }

    double fSampleRate = 48000.0;
    int fDecimation = 4;
    int fMinLag = 84, fMaxLag = 720;
    int fInterval = 96;
    Biquad fAntiAlias[2];
    std::array<float, kHistory> fHistory {};
    int fWrite = 0;
    int fPhase = 0;
    int fSinceUpdate = 0;
    double fPeriod = 0.0;
    float fConfidence = 0.0f;
};

} // namespace namsayin
