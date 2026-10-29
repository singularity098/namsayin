// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace xythos {

// Polyphase IIR half-band filters (two parallel chains of first-order allpasses, as in Laurent de Soras' HIIR),
// used to run nonlinear stages at 2x or 4x the sample rate. They're minimum-phase-like IIR filters: a few samples of
// group delay at high frequencies, no added latency, and no FIR pre-ringing.
namespace halfband {

// The allpass coefficients for `count` coefficients and a normalized transition band (0..0.5 of the sample rate).
// Non-RT (loops with pow/sin), but cheap.
template <int N>
std::array<double, N> design(double transition)
{
    double k = std::tan((1.0 - transition * 2.0) * M_PI / 4.0);
    k *= k;
    const double kksqrt = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = N * 2 + 1;

    std::array<double, N> coefs {};
    for (int index = 0; index < N; ++index)
    {
        const int c = index + 1;
        double num = 0.0;
        for (int i = 0, sign = 1;; ++i, sign = -sign)
        {
            const double term = std::pow(q, i * (i + 1)) * std::sin((i * 2 + 1) * c * M_PI / order) * sign;
            num += term;
            if (std::fabs(term) < 1e-100 || i > 100)
                break;
        }
        double den = 0.0;
        for (int i = 1, sign = -1;; ++i, sign = -sign)
        {
            const double term = std::pow(q, i * i) * std::cos(i * 2 * c * M_PI / order) * sign;
            den += term;
            if (std::fabs(term) < 1e-100 || i > 100)
                break;
        }
        const double ww = num * std::pow(q, 0.25) / (den + 0.5);
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[index] = (1.0 - x) / (1.0 + x);
    }
    return coefs;
}

// The two allpass chains, shared by the up- and downsampler. N must be even.
template <int N>
class Chains {
public:
    void setCoefs(const std::array<double, N>& coefs)
    {
        for (int i = 0; i < N; ++i)
            fCoef[i] = static_cast<float>(coefs[i]);
    }

    void reset()
    {
        fX.fill(0.0f);
        fY.fill(0.0f);
    }

    // a: the even-indexed chain, b: the odd-indexed one
    void process(float& a, float& b)
    {
        for (int i = 0; i < N; i += 2)
        {
            const float xa = fX[i], xb = fX[i + 1];
            fX[i] = a;
            fX[i + 1] = b;
            a = (a - fY[i]) * fCoef[i] + xa;
            b = (b - fY[i + 1]) * fCoef[i + 1] + xb;
            fY[i] = a;
            fY[i + 1] = b;
        }
    }

private:
    std::array<float, N> fCoef {};
    std::array<float, N> fX {};
    std::array<float, N> fY {};
};

} // namespace halfband

// Runs a per-sample function at 4x the sample rate, for one channel. Two half-band stages each way: a steep one
// between 1x and 2x (flat to 0.49 of the sample rate, > 70 dB down above 0.54), a cheaper one between 2x and 4x.
class Oversampler4x {
public:
    // Non-RT
    void prepare()
    {
        const auto steep = halfband::design<kSteep>(0.05);
        const auto light = halfband::design<kLight>(0.24);
        fUp1.setCoefs(steep);
        fDown1.setCoefs(steep);
        fUp2.setCoefs(light);
        fDown2.setCoefs(light);
        reset();
    }

    void reset()
    {
        fUp1.reset();
        fDown1.reset();
        fUp2.reset();
        fDown2.reset();
    }

    // RT. `f` maps one sample at the 4x rate.
    template <typename F>
    float process(float x, F&& f)
    {
        // 1x -> 2x
        float a = x, b = x;
        fUp1.process(a, b);
        float up[2] = { a, b };
        // 2x -> 4x, then the function, then 4x -> 2x
        float mid[2];
        for (int i = 0; i < 2; ++i)
        {
            float c = up[i], d = up[i];
            fUp2.process(c, d);
            float e = f(c);
            float g = f(d);
            fDown2.process(g, e);
            mid[i] = 0.5f * (e + g);
        }
        // 2x -> 1x
        float p = mid[1], q = mid[0];
        fDown1.process(p, q);
        return 0.5f * (p + q);
    }

private:
    static constexpr int kSteep = 8;
    static constexpr int kLight = 4;
    halfband::Chains<kSteep> fUp1, fDown1;
    halfband::Chains<kLight> fUp2, fDown2;
};

} // namespace xythos
