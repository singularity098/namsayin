// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>
#include <cstdint>

namespace namsayin {

enum class LfoShape { Sine, Triangle, Square, SawUp, SawDown };

// Phase-accumulator LFO for the modulation blocks. The phase runs 0..1; read it at any offset (stereo spread),
// in any shape. Rate changes keep the phase, so they never jump.
class Lfo {
public:
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        setRate(fHz);
    }

    void setRate(double hz)
    {
        fHz = hz;
        fStep = hz / fSampleRate;
    }

    void reset(double phase = 0.0) { fPhase = phase; }

    // Advances one sample; returns the new phase
    double advance()
    {
        fPhase += fStep;
        if (fPhase >= 1.0)
            fPhase -= 1.0;
        return fPhase;
    }

    double phase() const { return fPhase; }

    // -1..1 at `phase` (any value; wrapped)
    static float shape(LfoShape shape, double phase)
    {
        double p = phase - std::floor(phase);
        switch (shape)
        {
        case LfoShape::Sine:
            return static_cast<float>(std::sin(2.0 * M_PI * p));
        case LfoShape::Triangle:
            // starts at 0 rising, like the sine
            p += 0.25;
            p -= std::floor(p);
            return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
        case LfoShape::Square: {
            // a square with rounded edges (about 1/16 of a cycle), so it doesn't click
            const double s = 4.0 * std::sin(2.0 * M_PI * p);
            return static_cast<float>(s > 1.0 ? 1.0 : (s < -1.0 ? -1.0 : s));
        }
        case LfoShape::SawUp:
            return static_cast<float>(2.0 * p - 1.0);
        case LfoShape::SawDown:
            return static_cast<float>(1.0 - 2.0 * p);
        }
        return 0.0f;
    }

private:
    double fSampleRate = 48000.0;
    double fHz = 1.0;
    double fStep = 1.0 / 48000.0;
    double fPhase = 0.0;
};

// Smoothed random values (for drift in the doubler and a "random" LFO): a new target every period, approached with a
// raised-cosine glide. Deterministic per seed.
class RandomLfo {
public:
    void prepare(double sampleRate, double hz, uint32_t seed)
    {
        fStep = hz / sampleRate;
        fState = seed ? seed : 1u;
        fFrom = 0.0f;
        fTo = nextRandom();
        fPhase = 0.0;
    }

    void setRate(double sampleRate, double hz) { fStep = hz / sampleRate; }

    // -1..1
    float next()
    {
        fPhase += fStep;
        if (fPhase >= 1.0)
        {
            fPhase -= 1.0;
            fFrom = fTo;
            fTo = nextRandom();
        }
        const float t = static_cast<float>(0.5 - 0.5 * std::cos(M_PI * fPhase));
        return fFrom + (fTo - fFrom) * t;
    }

private:
    float nextRandom()
    {
        // xorshift32
        fState ^= fState << 13;
        fState ^= fState >> 17;
        fState ^= fState << 5;
        return static_cast<float>(fState) / 2147483648.0f - 1.0f;
    }

    double fStep = 0.0;
    double fPhase = 0.0;
    uint32_t fState = 1;
    float fFrom = 0.0f, fTo = 0.0f;
};

} // namespace namsayin
