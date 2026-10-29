// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace xythos {

// Pitch detection with the McLeod Pitch Method (as in Fretbored), refined for sub-cent accuracy.
//
// 1. The normalized square difference function (NSDF) is computed from an FFT autocorrelation, and the first of its
//    key maxima within kKeyMaxRatio of the highest gives the period (avoids octave errors).
// 2. That estimate is refined with a Hann-weighted normalized cross-correlation over a whole number of periods (so
//    neither the partial period at the ends nor a change of level biases it), low-passed above the first few
//    harmonics (so broadband noise doesn't blur its peak), measured at the largest multiple of the period that
//    fits, and divided back. Parabolic interpolation of a peak k periods out has 1/k of its error. The refinement uses a
//    longer span of audio than the detection (up to 4 windows) when the note has lasted that long, which makes it
//    much less sensitive to noise: the error falls with the span to the power 1.5.
//
// Not real-time safe (allocates in prepare(), and detect() is too slow for the audio thread): runs on a worker.
class PitchDetector {
public:
    static constexpr double kKeyMaxRatio = 0.9;
    // A span is only used for the refinement if the note correlates this well across it
    static constexpr double kMinRefineCorrelation = 0.9;
    // The refinement low-passes the signal at this many times the detected frequency
    static constexpr double kRefineCutoffHarmonics = 6.0;

    struct Result {
        double frequency = 0.0; // Hz; 0 if no pitch was found
        double clarity = 0.0;   // 0..1, how periodic the signal is
        double rms = 0.0;
    };

    static constexpr uint32_t kSpanWindows = 4;

    // `windowSize` must be a power of two
    void prepare(double sampleRate, uint32_t windowSize, double minFrequency, double maxFrequency);

    uint32_t windowSize() const { return fSize; }
    uint32_t maxSpan() const { return fSize * kSpanWindows; }

    // Analyses the last windowSize() of `count` samples (count >= windowSize()); the refinement may use all of them,
    // up to maxSpan()
    Result detect(const float* samples, uint32_t count);

private:
    double nsdfPeak(double lag, int searchRadius, double& value) const;
    // Refines `period` on the last `span` samples; returns false if the signal there isn't periodic enough
    bool refine(const float* samples, int span, double& period);
    double crossCorrelation(int lag, int length) const;
    void fft(std::vector<std::complex<double>>& data, bool inverse) const;

    double fSampleRate = 48000.0;
    uint32_t fSize = 0;
    int fMinLag = 2, fMaxLag = 2;
    std::vector<double> fX, fNsdf, fSpan;
    std::vector<std::complex<double>> fSpectrum, fTwiddles;
    std::vector<uint32_t> fBitReverse;
};

} // namespace xythos
