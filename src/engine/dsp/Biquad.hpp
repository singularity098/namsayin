// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>

namespace xythos {

// Biquad filter (transposed direct form II) with RBJ cookbook designs
class Biquad {
public:
    void setLowShelf(double sampleRate, double freq, double gainDb, double slope = 1.0)
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
        const double sa = 2.0 * std::sqrt(a) * alpha;
        set(a * ((a + 1) - (a - 1) * cw + sa),
            2 * a * ((a - 1) - (a + 1) * cw),
            a * ((a + 1) - (a - 1) * cw - sa),
            (a + 1) + (a - 1) * cw + sa,
            -2 * ((a - 1) + (a + 1) * cw),
            (a + 1) + (a - 1) * cw - sa);
    }

    void setHighShelf(double sampleRate, double freq, double gainDb, double slope = 1.0)
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
        const double sa = 2.0 * std::sqrt(a) * alpha;
        set(a * ((a + 1) + (a - 1) * cw + sa),
            -2 * a * ((a - 1) + (a + 1) * cw),
            a * ((a + 1) + (a - 1) * cw - sa),
            (a + 1) - (a - 1) * cw + sa,
            2 * ((a - 1) - (a + 1) * cw),
            (a + 1) - (a - 1) * cw - sa);
    }

    void setPeak(double sampleRate, double freq, double gainDb, double q)
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * q);
        set(1 + alpha * a, -2 * cw, 1 - alpha * a, 1 + alpha / a, -2 * cw, 1 - alpha / a);
    }

    void setHighPass(double sampleRate, double freq, double q = M_SQRT1_2)
    {
        const double w = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * q);
        set((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void setLowPass(double sampleRate, double freq, double q = M_SQRT1_2)
    {
        const double w = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * q);
        set((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    float process(float x)
    {
        const double y = fB0 * x + fZ1;
        fZ1 = fB1 * x - fA1 * y + fZ2;
        fZ2 = fB2 * x - fA2 * y;
        return static_cast<float>(y);
    }

    void reset() { fZ1 = fZ2 = 0.0; }

private:
    void set(double b0, double b1, double b2, double a0, double a1, double a2)
    {
        fB0 = b0 / a0;
        fB1 = b1 / a0;
        fB2 = b2 / a0;
        fA1 = a1 / a0;
        fA2 = a2 / a0;
    }

    double fB0 = 1.0, fB1 = 0.0, fB2 = 0.0, fA1 = 0.0, fA2 = 0.0;
    double fZ1 = 0.0, fZ2 = 0.0;
};

} // namespace xythos
