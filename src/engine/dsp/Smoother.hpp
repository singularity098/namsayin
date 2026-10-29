// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>

namespace xythos {

// One-pole smoothing of a control value, to avoid zipper noise
class Smoother {
public:
    void prepare(double sampleRate, double timeSeconds = 0.02)
    {
        fCoef = static_cast<float>(1.0 - std::exp(-1.0 / (timeSeconds * sampleRate)));
    }

    void setTarget(float target) { fTarget = target; }
    void snap(float value) { fTarget = fValue = value; }

    float next()
    {
        fValue += (fTarget - fValue) * fCoef;
        return fValue;
    }

    // For values that need a filter redesign: moves one step and returns true, or returns false once arrived.
    // Snaps when float precision stalls it short of the target (large values like 20000 Hz).
    bool advance()
    {
        if (settled())
            return false;
        const float before = fValue;
        next();
        if (fValue == before)
            snap(fTarget);
        return true;
    }

    float value() const { return fValue; }
    float target() const { return fTarget; }
    bool settled() const { return std::fabs(fTarget - fValue) < 1e-6f; }

private:
    float fCoef = 1.0f;
    float fValue = 0.0f;
    float fTarget = 0.0f;
};

inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

inline float gainToDb(float gain, float floorDb = -120.0f)
{
    return gain > 0.0f ? std::fmax(20.0f * std::log10(gain), floorDb) : floorDb;
}

} // namespace xythos
