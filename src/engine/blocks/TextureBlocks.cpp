// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/TextureBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

constexpr int kChaosTick = 16;          // samples per attractor step
constexpr double kChaosPitchMs = 3.0;   // the vibrato's widest swing
constexpr double kChaosDelayMs = 4.0;   // ... around this delay

// The Lorenz system, stepped with a few Euler steps per tick (its steps are tiny at audio rates)
struct Lorenz {
    double x = 1.0, y = 1.0, z = 20.0;

    void step(double dt, double rho)
    {
        constexpr double kSigma = 10.0, kBeta = 8.0 / 3.0;
        constexpr int kSub = 4;
        const double h = dt / kSub;
        for (int k = 0; k < kSub; ++k)
        {
            const double dx = kSigma * (y - x);
            const double dy = x * (rho - z) - y;
            const double dz = x * y - kBeta * z;
            x += h * dx;
            y += h * dy;
            z += h * dz;
        }
        // never let rounding carry it off (it stays well inside these on its attractor)
        x = std::clamp(x, -60.0, 60.0);
        y = std::clamp(y, -80.0, 80.0);
        z = std::clamp(z, 0.0, 120.0);
    }
};

class ChaosModBlock : public Block {
public:
    ChaosModBlock() { fMix.snap(1.0f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>((kChaosDelayMs + kChaosPitchMs + 1.0) / 1000.0 * sampleRate) + 8);
        fSmooth = 1.0f - EnvelopeFollower::coef(sampleRate, 5.0);
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kChaosParamSpeed:     fSpeed = value; break;
        case kChaosParamChaos:     fRho = 25.0 + 20.0 * value / 100.0; break;
        case kChaosParamFilter:    fFilter = value / 100.0f; break;
        case kChaosParamCutoff:    fCutoff = value; break;
        case kChaosParamResonance: fQ = value; break;
        case kChaosParamVolume:    fVolume = value / 100.0f; break;
        case kChaosParamPitch:     fPitch = value / 100.0f; break;
        case kChaosParamWidth:     fWidth = value / 100.0f; break;
        case kChaosParamMix:       fMix.setTarget(value / 100.0f); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
        }
        const double base = kChaosDelayMs / 1000.0 * fSampleRate;
        const double swing = kChaosPitchMs / 1000.0 * fSampleRate * fPitch;
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (--fUntilTick <= 0)
            {
                fUntilTick = kChaosTick;
                const double dt = fSpeed * kChaosTick / fSampleRate;
                for (int a = 0; a < 2; ++a)
                {
                    fAttractor[a].step(dt, fRho);
                    // each coordinate scaled to about -1..1 on the attractor
                    fTarget[a][0] = static_cast<float>(std::clamp(fAttractor[a].x / 20.0, -1.0, 1.0));
                    fTarget[a][1] = static_cast<float>(std::clamp(fAttractor[a].y / 27.0, -1.0, 1.0));
                    fTarget[a][2] = static_cast<float>(std::clamp((fAttractor[a].z - 24.0) / 20.0, -1.0, 1.0));
                }
            }
            const float mix = fMix.next();
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                // the right channel blends from the first attractor to its own with Width
                float m[3];
                for (int c = 0; c < 3; ++c)
                {
                    const float target = ch == 0 ? fTarget[0][c] : fTarget[0][c] + (fTarget[1][c] - fTarget[0][c]) * fWidth;
                    fValue[ch][c] += (target - fValue[ch][c]) * fSmooth;
                    m[c] = fValue[ch][c];
                }
                const float x = io[ch][i];
                fLines[ch].write(x);
                float y = swing > 0.0 ? fLines[ch].readFractional(static_cast<float>(base + swing * m[2])) : x;
                if (fFilter > 0.0f)
                {
                    const double cutoff = fCutoff * std::exp2(3.0 * fFilter * m[0]);
                    fFilters[ch].set(fSampleRate, std::clamp(cutoff, 30.0, 0.45 * fSampleRate), fQ);
                    y = fFilters[ch].process(y).low;
                }
                y *= 1.0f - fVolume * 0.5f * (1.0f + m[1]);
                io[ch][i] = x + (y - x) * mix;
            }
        }
    }

    void reset() override
    {
        fAttractor[0] = Lorenz {};
        fAttractor[1] = Lorenz { 1.001, 1.0, 20.0 };
        for (int ch = 0; ch < 2; ++ch)
        {
            fLines[ch].reset();
            fFilters[ch].reset();
            for (float& v : fValue[ch])
                v = 0.0f;
        }
        fUntilTick = 0;
        fSnap = true;
    }

private:
    double fSampleRate = 48000.0;
    float fSpeed = 0.8f;
    double fRho = 37.0;
    float fFilter = 0.6f, fCutoff = 1200.0f, fQ = 2.0f, fVolume = 0.0f, fPitch = 0.0f, fWidth = 0.5f;
    Smoother fMix;
    Lorenz fAttractor[2];
    float fTarget[2][3] = {};
    float fValue[2][3] = {};
    float fSmooth = 0.01f;
    int fUntilTick = 0;
    DelayLine fLines[2];
    Svf fFilters[2];
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kBodyNames[] = { "Dreadnought", "Jumbo", "Parlor", "Classical" };
constexpr const char* kPickupNames[] = { "Single Coil", "Humbucker", "Neutral" };

struct Resonance {
    double hz, db, q;
};

// Each body's resonances (air, top, the boxy dip, presence) and the string brightness it adds (a high shelf at 6 kHz)
constexpr Resonance kBodies[4][4] = {
    { { 98, 6.0, 2.0 }, { 200, 4.0, 1.5 }, { 450, -3.0, 1.0 }, { 2500, 3.0, 1.0 } },  // Dreadnought: big, deep
    { { 88, 7.0, 1.8 }, { 180, 4.5, 1.4 }, { 520, -2.0, 1.0 }, { 3000, 2.5, 1.0 } },  // Jumbo: boomier still
    { { 130, 4.0, 2.0 }, { 250, 4.0, 1.5 }, { 700, 2.0, 1.0 }, { 3500, 3.0, 1.2 } },  // Parlor: small, midrange
    { { 100, 5.0, 2.0 }, { 210, 4.0, 1.5 }, { 1000, -2.0, 1.0 }, { 2000, -1.0, 1.0 } }, // Classical: nylon, warm
};
constexpr double kBodySparkle[4] = { 4.0, 3.0, 4.0, -4.0 };
constexpr double kReflectionsMs[3] = { 1.3, 2.9, 4.7 };

class AcousticBlock : public Block {
public:
    AcousticBlock()
    {
        fMix.snap(1.0f);
        fLevel.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fLevel.prepare(sampleRate);
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(kReflectionsMs[2] / 1000.0 * sampleRate) + 8);
        for (int k = 0; k < 3; ++k)
            fReflections[k] = std::max<size_t>(1, static_cast<size_t>(kReflectionsMs[k] / 1000.0 * sampleRate));
        fUpdate = true;
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kAcousticParamBody:      fBody = std::clamp(static_cast<int>(value), 0, 3); break;
        case kAcousticParamPickup:    fPickup = std::clamp(static_cast<int>(value), 0, 2); break;
        case kAcousticParamResonance: fResonance = value / 100.0f; break;
        case kAcousticParamTop:       fTop = value; break;
        case kAcousticParamBass:      fBass = value; break;
        case kAcousticParamWidth:     fWidth = value / 100.0f; return;
        case kAcousticParamMix:       fMix.setTarget(value / 100.0f); return;
        case kAcousticParamLevel:     fLevel.setTarget(dbToGain(value)); return;
        }
        fUpdate = true;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fUpdate)
            updateFilters();
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
            fLevel.snap(fLevel.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mix = fMix.next(), level = fLevel.next();
            float y[2] = { 0.0f, 0.0f };
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                float v = io[ch][i];
                for (Biquad& filter : fChain[ch])
                    v = filter.process(v);
                y[ch] = v * fGain;
            }
            // width: short reflections from the room around the guitar, added to one side and taken from the other
            const float mid = numChannels > 1 ? 0.5f * (y[0] + y[1]) : y[0];
            fLines[0].write(mid);
            const float side = fWidth
                               * (0.35f * fLines[0].read(fReflections[0]) - 0.25f * fLines[0].read(fReflections[1])
                                  + 0.18f * fLines[0].read(fReflections[2]));
            if (numChannels == 1)
            {
                io[0][i] = (io[0][i] + (y[0] - io[0][i]) * mix) * level;
            }
            else
            {
                const float left = y[0] + side, right = y[1] - side;
                io[0][i] = (io[0][i] + (left - io[0][i]) * mix) * level;
                io[1][i] = (io[1][i] + (right - io[1][i]) * mix) * level;
            }
        }
    }

    void reset() override
    {
        for (auto& chain : fChain)
            for (Biquad& filter : chain)
                filter.reset();
        for (DelayLine& line : fLines)
            line.reset();
        fSnap = true;
    }

private:
    static constexpr int kFilters = 9;

    void updateFilters()
    {
        fUpdate = false;
        const double nyquist = 0.45 * fSampleRate;
        for (auto& chain : fChain)
        {
            int f = 0;
            // undo the pickup: its mid hump, its treble peak and the roll-off above it
            const double peak = fPickup == 0 ? 4000.0 : 2500.0;
            const bool neutral = fPickup == 2;
            chain[f++].setPeak(fSampleRate, 500.0, neutral ? 0.0 : -4.0, 0.8);
            chain[f++].setPeak(fSampleRate, std::min(peak, nyquist), neutral ? 0.0 : -5.0, 1.5);
            chain[f++].setHighShelf(fSampleRate, std::min(peak * 1.6, nyquist), neutral ? 0.0 : 8.0);
            // the body
            for (const Resonance& r : kBodies[fBody])
                chain[f++].setPeak(fSampleRate, std::min(r.hz, nyquist), r.db * fResonance, r.q);
            chain[f++].setHighShelf(fSampleRate, std::min(6000.0, nyquist), kBodySparkle[fBody] + fTop);
            chain[f++].setLowShelf(fSampleRate, 150.0, fBass);
        }
        // the body peaks add level; take it back off so the block stays near unity
        fGain = dbToGain(-3.0f * fResonance);
    }

    double fSampleRate = 48000.0;
    int fBody = 0, fPickup = 0;
    float fResonance = 1.0f, fTop = 3.0f, fBass = 0.0f, fWidth = 0.4f;
    float fGain = 1.0f;
    bool fUpdate = true, fSnap = true;
    Smoother fMix, fLevel;
    std::array<std::array<Biquad, kFilters>, 2> fChain;
    DelayLine fLines[1];
    size_t fReflections[3] = { 60, 140, 220 };
};

} // namespace

const BlockDescriptor kChaosModBlockDescriptor = {
    "chaosmod",
    "Chaos Mod",
    Category::Modulation,
    kChaosParamCount,
    {
        ParamSpec { "Speed", "", 0.05f, 10.0f, 0.8f, ParamScale::Log },
        ParamSpec { "Chaos", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Filter", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Cutoff", "Hz", 100.0f, 16000.0f, 1200.0f, ParamScale::Log },
        ParamSpec { "Resonance", "", 0.5f, 10.0f, 2.0f, ParamScale::Log },
        ParamSpec { "Volume", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Pitch", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ChaosModBlock()); },
};

const BlockDescriptor kAcousticBlockDescriptor = {
    "acoustic",
    "Acoustic Sim",
    Category::Eq,
    kAcousticParamCount,
    {
        ParamSpec { "Body", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kBodyNames },
        ParamSpec { "Pickup", "", 0.0f, 2.0f, 1.0f, ParamScale::Choice, kPickupNames },
        ParamSpec { "Resonance", "%", 0.0f, 150.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Top", "dB", -6.0f, 12.0f, 3.0f, ParamScale::Linear },
        ParamSpec { "Bass", "dB", -12.0f, 6.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new AcousticBlock()); },
};

} // namespace namsayin
