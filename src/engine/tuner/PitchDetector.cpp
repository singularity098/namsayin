// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/tuner/PitchDetector.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

void PitchDetector::prepare(double sampleRate, uint32_t windowSize, double minFrequency, double maxFrequency)
{
    fSampleRate = sampleRate;
    fSize = windowSize;
    fMinLag = std::max(2, static_cast<int>(std::floor(sampleRate / maxFrequency)));
    fMaxLag = std::min(static_cast<int>(windowSize) - 2, static_cast<int>(std::ceil(sampleRate / minFrequency)) + 2);
    fX.assign(windowSize, 0.0);
    fNsdf.assign(windowSize, 0.0);
    fSpan.assign(maxSpan(), 0.0);

    // FFT of twice the window, so the autocorrelation doesn't wrap around
    const uint32_t n = windowSize * 2;
    fSpectrum.assign(n, 0.0);
    fTwiddles.resize(n / 2);
    for (uint32_t i = 0; i < n / 2; ++i)
        fTwiddles[i] = std::polar(1.0, -2.0 * M_PI * i / n);
    fBitReverse.resize(n);
    uint32_t bits = 0;
    while ((1u << bits) < n)
        ++bits;
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t r = 0;
        for (uint32_t b = 0; b < bits; ++b)
            r |= ((i >> b) & 1u) << (bits - 1 - b);
        fBitReverse[i] = r;
    }
}

void PitchDetector::fft(std::vector<std::complex<double>>& data, bool inverse) const
{
    const uint32_t n = static_cast<uint32_t>(data.size());
    for (uint32_t i = 0; i < n; ++i)
    {
        if (fBitReverse[i] > i)
            std::swap(data[i], data[fBitReverse[i]]);
    }
    for (uint32_t size = 2; size <= n; size <<= 1)
    {
        const uint32_t half = size / 2;
        const uint32_t step = n / size;
        for (uint32_t start = 0; start < n; start += size)
        {
            for (uint32_t j = 0; j < half; ++j)
            {
                const std::complex<double> w = inverse ? std::conj(fTwiddles[j * step]) : fTwiddles[j * step];
                const std::complex<double> t = data[start + j + half] * w;
                data[start + j + half] = data[start + j] - t;
                data[start + j] += t;
            }
        }
    }
    if (inverse)
    {
        for (std::complex<double>& v : data)
            v /= static_cast<double>(n);
    }
}

PitchDetector::Result PitchDetector::detect(const float* const all, uint32_t count)
{
    Result result;
    const int n = static_cast<int>(fSize);
    const float* const samples = all + count - fSize;

    double mean = 0.0;
    for (int i = 0; i < n; ++i)
        mean += samples[i];
    mean /= n;
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        fX[i] = samples[i] - mean;
        energy += fX[i] * fX[i];
    }
    result.rms = std::sqrt(energy / n);
    if (energy <= 0.0)
        return result;

    // Autocorrelation r(t) through |FFT|^2
    std::fill(fSpectrum.begin(), fSpectrum.end(), 0.0);
    for (int i = 0; i < n; ++i)
        fSpectrum[i] = fX[i];
    fft(fSpectrum, false);
    for (std::complex<double>& v : fSpectrum)
        v = std::norm(v);
    fft(fSpectrum, true);

    // NSDF n(t) = 2 r(t) / m(t), with m(t) the energy of both overlapping parts
    double m = 2.0 * energy;
    fNsdf[0] = 1.0;
    for (int t = 1; t < n; ++t)
    {
        m -= fX[t - 1] * fX[t - 1] + fX[n - t] * fX[n - t];
        fNsdf[t] = m > 1e-12 ? 2.0 * fSpectrum[t].real() / m : 0.0;
    }

    // Key maxima: the highest point of each positive region after the first zero crossing
    int t = 1;
    while (t < fMaxLag && fNsdf[t] > 0.0)
        ++t;
    int peaks[64];
    int numPeaks = 0;
    bool positive = false;
    int best = -1;
    for (; t < fMaxLag && numPeaks < 64; ++t)
    {
        if (fNsdf[t] > 0.0)
        {
            if (!positive)
            {
                positive = true;
                best = -1;
            }
            if (best < 0 || fNsdf[t] > fNsdf[best])
                best = t;
        }
        else if (positive)
        {
            positive = false;
            if (best >= fMinLag)
                peaks[numPeaks++] = best;
        }
    }
    if (positive && best >= fMinLag && numPeaks < 64)
        peaks[numPeaks++] = best;
    if (numPeaks == 0)
        return result;

    double highest = 0.0;
    for (int i = 0; i < numPeaks; ++i)
        highest = std::max(highest, fNsdf[peaks[i]]);
    int chosen = peaks[0];
    for (int i = 0; i < numPeaks; ++i)
    {
        if (fNsdf[peaks[i]] >= kKeyMaxRatio * highest)
        {
            chosen = peaks[i];
            break;
        }
    }

    double clarity = 0.0;
    double period = nsdfPeak(chosen, 0, clarity);
    result.clarity = std::min(1.0, clarity);

    // Refinement over the longest span that still holds the same note
    const int longest = static_cast<int>(std::min(count, maxSpan()));
    for (int span = longest; span >= n; span /= 2)
    {
        double refined = period;
        if (refine(all + count - span, span, refined))
        {
            period = refined;
            break;
        }
    }

    result.frequency = fSampleRate / period;
    return result;
}

double PitchDetector::nsdfPeak(double lag, int searchRadius, double& value) const
{
    int p = static_cast<int>(std::lround(lag));
    for (int i = p - searchRadius; i <= p + searchRadius; ++i)
    {
        if (i > 0 && i < static_cast<int>(fSize) - 1 && fNsdf[i] > fNsdf[p])
            p = i;
    }
    const double a = fNsdf[p - 1], b = fNsdf[p], c = fNsdf[p + 1];
    const double denom = a - 2.0 * b + c;
    const double shift = denom != 0.0 ? (a - c) / (2.0 * denom) : 0.0;
    value = b - (a - c) * shift / 4.0;
    return p + shift;
}

bool PitchDetector::refine(const float* samples, int span, double& period)
{
    double mean = 0.0;
    for (int i = 0; i < span; ++i)
        mean += samples[i];
    mean /= span;
    for (int i = 0; i < span; ++i)
        fSpan[i] = samples[i] - mean;

    // Low-pass above the first few harmonics: the period stays the same, but broadband noise no longer blurs the
    // correlation peak (which matters most for low notes, whose peak is broad). 4th order Butterworth.
    const double cutoff = kRefineCutoffHarmonics * fSampleRate / period;
    if (cutoff < 0.4 * fSampleRate)
    {
        const double w = 2.0 * M_PI * cutoff / fSampleRate;
        for (const double q : { 0.5411961, 1.3065630 })
        {
            const double alpha = std::sin(w) / (2.0 * q), cw = std::cos(w), a0 = 1.0 + alpha;
            const double b0 = (1.0 - cw) / 2.0 / a0, b1 = (1.0 - cw) / a0, a1 = -2.0 * cw / a0;
            const double a2 = (1.0 - alpha) / a0;
            double z1 = 0.0, z2 = 0.0;
            for (int i = 0; i < span; ++i)
            {
                const double x = fSpan[i];
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b0 * x - a2 * y;
                fSpan[i] = y;
            }
        }
            // skip the filter's start-up
        const int warmup = std::min(span / 4, static_cast<int>(4.0 * fSampleRate / cutoff));
        std::copy(fSpan.begin() + warmup, fSpan.begin() + span, fSpan.begin());
        span -= warmup;
    }

    // Correlate a whole number of periods (about half the span) with itself k periods later. Twice, as the
    // whole number of periods depends on the period.
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        const int periods = std::max(1, static_cast<int>(span / 2 / period));
        const int length = static_cast<int>(std::lround(periods * period));
        const int k = static_cast<int>((span - 2 - length) / period);
        if (k < 1)
            return false;
        const int centre = static_cast<int>(std::lround(k * period));

        // Climb to the local maximum near k periods (the coarse period's error is multiplied by k), staying within
        // a quarter period so it's the same peak; then a parabola through it and its neighbours
        const int radius = std::max(2, static_cast<int>(period / 4));
        int p = centre;
        double cp = crossCorrelation(p, length);
        for (const int step : { -1, 1 })
        {
            for (int i = p + step; std::abs(i - centre) <= radius && i + 1 + length <= span && i > 1; i += step)
            {
                const double c = crossCorrelation(i, length);
                if (c <= cp)
                    break;
                p = i;
                cp = c;
            }
        }
        if (p < 2 || p + 1 + length > span || cp < kMinRefineCorrelation)
            return false;
        const double a = crossCorrelation(p - 1, length), c = crossCorrelation(p + 1, length);
        const double denom = a - 2.0 * cp + c;
        const double shift = denom < 0.0 ? std::clamp((a - c) / (2.0 * denom), -0.5, 0.5) : 0.0;
        period = (p + shift) / k;
    }
    return true;
}

double PitchDetector::crossCorrelation(int lag, int length) const
{
    // Hann-weighted, so changes of level within the span (an attack, a decay) don't bias the peak
    const double* x = fSpan.data();
    const double step = 2.0 * M_PI / length;
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(step * (i + 0.5));
        xy += w * x[i] * x[i + lag];
        xx += w * x[i] * x[i];
        yy += w * x[i + lag] * x[i + lag];
    }
    return xx > 0.0 && yy > 0.0 ? xy / std::sqrt(xx * yy) : 0.0;
}

} // namespace namsayin
