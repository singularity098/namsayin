// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/tuner/Tuner.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

void Tuner::prepare(double sampleRate)
{
    fSampleRate = sampleRate;
    uint32_t window = 1;
    while (window < kWindowSeconds * sampleRate)
        window <<= 1;
    fDetector.prepare(sampleRate, window, kMinFrequency, kMaxFrequency);

    // Twice the longest span, so the audio thread can keep writing while a span is copied out
    const uint32_t size = fDetector.maxSpan() * 2;
    fRing.reset(new std::atomic<float>[size]);
    for (uint32_t i = 0; i < size; ++i)
        fRing[i].store(0.0f, std::memory_order_relaxed);
    fRingMask = size - 1;
    fSpan.assign(fDetector.maxSpan(), 0.0f);
    fWritten.store(0, std::memory_order_relaxed);
    fStart.store(0, std::memory_order_relaxed);
}

void Tuner::push(const float* left, const float* right, uint32_t frames)
{
    uint64_t written = fWritten.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < frames; ++i, ++written)
    {
        const float x = right ? 0.5f * (left[i] + right[i]) : left[i];
        fRing[written & fRingMask].store(x, std::memory_order_relaxed);
    }
    fWritten.store(written, std::memory_order_release);
}

PitchDetector::Result Tuner::analyze()
{
    PitchDetector::Result none;
    if (!fRing)
        return none;

    const uint64_t written = fWritten.load(std::memory_order_acquire);
    const uint64_t start = fStart.load(std::memory_order_relaxed);
    const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(written - std::min(start, written),
                                                                    fDetector.maxSpan()));
    if (count < fDetector.windowSize())
        return none;

    const uint64_t first = written - count;
    for (uint32_t i = 0; i < count; ++i)
        fSpan[i] = fRing[(first + i) & fRingMask].load(std::memory_order_relaxed);
    // If the audio thread went all the way round the ring meanwhile, the copy is torn: skip this one
    if (fWritten.load(std::memory_order_acquire) - first > fRingMask + 1)
        return none;

    // Gate on the most recent window
    double energy = 0.0;
    const float* window = fSpan.data() + count - fDetector.windowSize();
    for (uint32_t i = 0; i < fDetector.windowSize(); ++i)
        energy += static_cast<double>(window[i]) * window[i];
    if (std::sqrt(energy / fDetector.windowSize()) < std::pow(10.0, kGateDb / 20.0))
        return none;

    // Detect on the latest window first
    PitchDetector::Result result = fDetector.detect(window, fDetector.windowSize());
    if (result.frequency <= 0.0 || result.clarity < kMinClarity)
        return none;

    // Only audio at a steady level is analysed: silence before the note, its attack or a fade at its end would
    // bias the result. Levels are compared in chunks of whole periods (so the phase doesn't matter) of at least
    // kChunkSeconds. A window that isn't steady gives no reading at all (a note starting, stopping or changing);
    // the refinement may use more than one window of steady audio.
    const double period = fSampleRate / result.frequency;
    const uint32_t chunk = std::min(fDetector.windowSize() / 2, static_cast<uint32_t>(std::lround(
        std::ceil(kChunkSeconds * fSampleRate / period) * period)));
    auto chunkEnergy = [&](uint32_t end) {
        double e = 0.0;
        for (uint32_t i = end - chunk; i < end; ++i)
            e += static_cast<double>(fSpan[i]) * fSpan[i];
        return e;
    };
    const double latest = chunkEnergy(count);
    uint32_t steady = chunk;
    while (steady + chunk <= count)
    {
        const double e = chunkEnergy(count - steady);
        if (e < latest / (kSteadyRatio * kSteadyRatio) || e > latest * kSteadyRatio * kSteadyRatio)
            break;
        steady += chunk;
    }
    if (steady < fDetector.windowSize())
        return none;
    if (steady > fDetector.windowSize())
        result = fDetector.detect(fSpan.data() + count - steady, steady);
    if (result.clarity < kMinClarity)
        result.frequency = 0.0;
    return result;
}

} // namespace namsayin
