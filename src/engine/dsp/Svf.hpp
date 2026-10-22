// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <algorithm>
#include <cmath>

namespace namsayin {

// State variable filter, topology-preserving transform (Zavalishin). Stays stable and smooth when the cutoff moves
// every sample, which the wah, envelope filter and synth need. All three outputs come from one step.
class Svf {
public:
    struct Out {
        float low, band, high;
    };

    // Q > 0. Safe to call every sample.
    void set(double sampleRate, double freq, double q)
    {
        const double f = std::clamp(freq, 1.0, 0.49 * sampleRate);
        fG = static_cast<float>(std::tan(M_PI * f / sampleRate));
        fK = static_cast<float>(1.0 / q);
        fA1 = 1.0f / (1.0f + fG * (fG + fK));
        fA2 = fG * fA1;
        fA3 = fG * fA2;
    }

    Out process(float x)
    {
        const float v3 = x - fIc2;
        const float v1 = fA1 * fIc1 + fA2 * v3;
        const float v2 = fIc2 + fA2 * fIc1 + fA3 * v3;
        fIc1 = 2.0f * v1 - fIc1;
        fIc2 = 2.0f * v2 - fIc2;
        return { v2, v1, x - fK * v1 - v2 };
    }

    float k() const { return fK; }

    void reset() { fIc1 = fIc2 = 0.0f; }

private:
    float fG = 0.0f, fK = 1.0f, fA1 = 1.0f, fA2 = 0.0f, fA3 = 0.0f;
    float fIc1 = 0.0f, fIc2 = 0.0f;
};

// First-order filters (one-pole low-pass, the matching high-pass, and a first-order allpass for phasers)
class OnePole {
public:
    void setLowPass(double sampleRate, double freq)
    {
        fA = static_cast<float>(std::exp(-2.0 * M_PI * std::min(freq, 0.49 * sampleRate) / sampleRate));
    }

    float lowPass(float x)
    {
        fZ = x + (fZ - x) * fA;
        return fZ;
    }

    float highPass(float x) { return x - lowPass(x); }

    void reset() { fZ = 0.0f; }

private:
    float fA = 0.0f;
    float fZ = 0.0f;
};

// Envelope follower: rectified input, separate attack and release
class EnvelopeFollower {
public:
    void prepare(double sampleRate, double attackMs, double releaseMs)
    {
        fAttack = coef(sampleRate, attackMs);
        fRelease = coef(sampleRate, releaseMs);
    }

    float process(float x)
    {
        const float a = std::fabs(x);
        fEnv = a + (fEnv - a) * (a > fEnv ? fAttack : fRelease);
        return fEnv;
    }

    float value() const { return fEnv; }
    void reset() { fEnv = 0.0f; }

    static float coef(double sampleRate, double ms)
    {
        return ms <= 0.0 ? 0.0f : static_cast<float>(std::exp(-1.0 / (ms / 1000.0 * sampleRate)));
    }

private:
    float fAttack = 0.0f, fRelease = 0.0f;
    float fEnv = 0.0f;
};

} // namespace namsayin
