// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace namsayin {

// Real-time monophonic pitch tracking from zero crossings, for the synth and the harmonizer: the guitar is band
// limited (a low-pass kept just above the note, so harmonics can't add crossings), then the time between upward
// zero crossings (armed by a swing past a fraction of the envelope, and interpolated between samples) is the
// period. A new pitch is accepted when two periods in a row agree, so a stray crossing doesn't make it jump. It
// follows within about two cycles and adds no latency; chords confuse it.
class PitchTracker {
public:
    static constexpr double kMinHz = 40.0;
    static constexpr double kMaxHz = 1500.0;

    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fHighPass.setHighPass(sampleRate, 60.0);
        fEnvelope.prepare(sampleRate, 1.0, 80.0);
        reset();
    }

    void reset()
    {
        fHighPass.reset();
        fLowPass.reset();
        fEnvelope.reset();
        fArmed = false;
        fPrevious = 0.0f;
        fSinceCrossing = 0.0;
        fLastPeriod = 0.0;
        fHz = 0.0;
        setLowPass(800.0);
    }

    // One input sample. Returns true when a new pitch was accepted.
    bool process(float in)
    {
        const float x = fLowPass.process(fHighPass.process(in));
        const float env = fEnvelope.process(x);
        fSinceCrossing += 1.0;
        bool accepted = false;
        if (x < -kHysteresis * env)
            fArmed = true;
        if (fArmed && fPrevious < 0.0f && x >= 0.0f && env > kGateLevel)
        {
            fArmed = false;
            const double fraction = fPrevious / (fPrevious - x); // where between the samples it crossed
            const double period = fSinceCrossing - 1.0 + fraction;
            const double hz = fSampleRate / period;
            if (hz >= kMinHz && hz <= kMaxHz)
            {
                if (fLastPeriod > 0.0 && std::fabs(period / fLastPeriod - 1.0) < 0.12)
                {
                    fHz = hz;
                    setLowPass(std::clamp(hz * 1.5, 120.0, 1800.0));
                    accepted = true;
                }
                fLastPeriod = period;
            }
            fSinceCrossing = 1.0 - fraction;
        }
        fPrevious = x;
        return accepted;
    }

    double hz() const { return fHz; } // 0 until a pitch is found
    float envelope() const { return fEnvelope.value(); }
    bool sounding() const { return fEnvelope.value() > kGateLevel; }

private:
    static constexpr float kHysteresis = 0.3f;
    static constexpr float kGateLevel = 0.003f;

    void setLowPass(double hz) { fLowPass.setLowPass(fSampleRate, std::min(hz, 0.45 * fSampleRate)); }

    double fSampleRate = 48000.0;
    Biquad fHighPass, fLowPass;
    EnvelopeFollower fEnvelope;
    bool fArmed = false;
    float fPrevious = 0.0f;
    double fSinceCrossing = 0.0;
    double fLastPeriod = 0.0;
    double fHz = 0.0;
};

} // namespace namsayin
