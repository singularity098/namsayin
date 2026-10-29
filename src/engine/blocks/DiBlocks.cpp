// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/DiBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Oversampler.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace xythos {

namespace {

// Knob changes redesign the filters (and the gains that go with them) every kUpdateInterval samples while they glide
constexpr uint32_t kUpdateInterval = 32;
constexpr int kMaxFilters = 12;

float dbToGainD(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

// A triode-like stage: the wave flattens sooner on one side (even harmonics, the tube's warmth); no output for no input
constexpr float kTubeBias = 0.15f;
const float kTubeOffset = shaper::asymmetric(kTubeBias, 1.0f, 0.6f);

float tube(float x)
{
    return shaper::asymmetric(x + kTubeBias, 1.0f, 0.6f) - kTubeOffset;
}

// What the DI boxes share: their knobs (smoothed, in their own units), filter banks, two oversamplers and a DC
// blocker per channel, and the loop. A box implements design() (filters and gains from the knobs) and sample() (one
// sample, one channel).
class DiBase : public Block {
public:
    explicit DiBase(const BlockDescriptor& descriptor)
        : fDescriptor(descriptor)
    {
        for (int i = 0; i < fDescriptor.numParams; ++i)
            fKnob[static_cast<size_t>(i)].snap(fDescriptor.params[static_cast<size_t>(i)].def);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (Smoother& knob : fKnob)
            knob.prepare(sampleRate / kUpdateInterval, 0.03);
        for (auto& pair : fOversampler)
            for (Oversampler4x& os : pair)
                os.prepare();
        for (OnePole& dc : fDcBlock)
            dc.setLowPass(sampleRate, 10.0);
        prepareBox(sampleRate);
        reset();
    }

    void setParam(int index, float value) override
    {
        if (index < 0 || index >= fDescriptor.numParams)
            return;
        const ParamScale scale = fDescriptor.params[static_cast<size_t>(index)].scale;
        Smoother& knob = fKnob[static_cast<size_t>(index)];
        if (scale == ParamScale::Choice || scale == ParamScale::Toggle)
        {
            knob.snap(value);
            fDesign = true;
        }
        else
        {
            knob.setTarget(value);
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            for (Smoother& knob : fKnob)
                knob.snap(knob.target());
            fDesign = true;
        }
        if (fDesign)
        {
            fDesign = false;
            design();
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (++fCounter >= kUpdateInterval)
            {
                fCounter = 0;
                bool moved = false;
                for (Smoother& knob : fKnob)
                    moved = knob.advance() || moved;
                if (moved)
                    design();
            }
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] = sample(ch, io[ch][i]);
        }
    }

    void reset() override
    {
        for (auto& pair : fOversampler)
            for (Oversampler4x& os : pair)
                os.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            for (Biquad& filter : fFilters[ch])
                filter.reset();
            for (OnePole& filter : fOnePoles[ch])
                filter.reset();
            fDcBlock[ch].reset();
        }
        resetBox();
        fSnap = true;
    }

protected:
    virtual void prepareBox(double /*sampleRate*/) {}
    virtual void resetBox() {}
    virtual void design() = 0;
    virtual float sample(uint32_t ch, float x) = 0;

    float knob(int index) const { return fKnob[static_cast<size_t>(index)].value(); }
    int choice(int index) const { return static_cast<int>(std::lround(knob(index))); }
    bool toggle(int index) const { return knob(index) >= 0.5f; }

    // Sets a filter to pass everything (a switched-off band: a peak of 0 dB)
    void flat(Biquad& filter) const { filter.setPeak(fSampleRate, 1000.0, 0.0, 1.0); }
    double nyquistSafe(double hz) const { return std::min(hz, 0.45 * fSampleRate); }

    // A bass speaker's voicing, for DI boxes that emulate a miked amp: no deep sub, a bump at the cab's resonance, a
    // dip in the boxy low mids, the cone's presence, and the steep roll-off above about 4.5 kHz. Five filters from
    // `first`.
    void designSpeaker(int ch, int first)
    {
        auto& f = fFilters[ch];
        f[first].setHighPass(fSampleRate, 45.0, 0.8);
        f[first + 1].setPeak(fSampleRate, 110.0, 3.0, 1.0);
        f[first + 2].setPeak(fSampleRate, 450.0, -4.0, 1.0);
        f[first + 3].setLowPass(fSampleRate, nyquistSafe(4500.0), 0.7);
        f[first + 4].setLowPass(fSampleRate, nyquistSafe(5500.0), 0.6);
    }

    float speaker(int ch, int first, float x)
    {
        auto& f = fFilters[ch];
        for (int k = first; k < first + 5; ++k)
            x = f[k].process(x);
        return x;
    }

    float dcBlock(uint32_t ch, float x) { return fDcBlock[ch].highPass(x); }

    double fSampleRate = 48000.0;
    Biquad fFilters[2][kMaxFilters];
    OnePole fOnePoles[2][4];
    Oversampler4x fOversampler[2][2];

private:
    const BlockDescriptor& fDescriptor;
    std::array<Smoother, kParamsPerSlot> fKnob;
    OnePole fDcBlock[2];
    uint32_t fCounter = 0;
    bool fDesign = true;
    bool fSnap = true;
};

// A simple feed-forward compressor for the boxes that have one: a peak detector, a soft knee, and an optical cell's
// program-dependent release (a fast part and a slow part, so short peaks recover quickly and long ones slowly)
class OptoCell {
public:
    void prepare(double sampleRate)
    {
        fDetector.prepare(sampleRate, 1.0, 60.0);
        fFastAttack = EnvelopeFollower::coef(sampleRate, 10.0);
        fFastRelease = EnvelopeFollower::coef(sampleRate, 80.0);
        fSlowAttack = EnvelopeFollower::coef(sampleRate, 300.0);
        fSlowRelease = EnvelopeFollower::coef(sampleRate, 1500.0);
    }

    void reset()
    {
        fDetector.reset();
        fFast = fSlow = 0.0f;
    }

    // The gain for this sample (linear), from the input level against the threshold (dBFS) at the ratio
    float gain(float x, float thresholdDb, float ratio)
    {
        const float env = fDetector.process(x);
        const float levelDb = 20.0f * std::log10(env + 1e-9f);
        // a 6 dB soft knee
        const float over = levelDb - thresholdDb;
        float excess = 0.0f;
        if (over > 3.0f)
            excess = over;
        else if (over > -3.0f)
            excess = (over + 3.0f) * (over + 3.0f) / 12.0f;
        const float target = excess * (1.0f - 1.0f / ratio);
        fFast = target + (fFast - target) * (target > fFast ? fFastAttack : fFastRelease);
        fSlow = target + (fSlow - target) * (target > fSlow ? fSlowAttack : fSlowRelease);
        fReduction = std::max(0.65f * fFast + 0.35f * fSlow, 0.0f);
        return std::exp(-fReduction * 0.11512925f); // ln(10) / 20
    }

    float reduction() const { return fReduction; } // dB

private:
    EnvelopeFollower fDetector;
    float fFastAttack = 0.0f, fFastRelease = 0.0f, fSlowAttack = 0.0f, fSlowRelease = 0.0f;
    float fFast = 0.0f, fSlow = 0.0f;
    float fReduction = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Studio DI

constexpr const char* kLowCutNames[] = { "Off", "30 Hz", "60 Hz", "100 Hz" };

class StudioDiBlock : public DiBase {
public:
    StudioDiBlock() : DiBase(kStudioDiBlockDescriptor) {}

protected:
    // filters: 0 low cut, 1 air shelf, 2 the transformer's top, 3 splits off the lows that saturate
    void design() override
    {
        static constexpr double kLowCut[] = { 0.0, 30.0, 60.0, 100.0 };
        const int cut = std::clamp(choice(kStudioDiLowCut), 0, 3);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            if (cut == 0)
                flat(f[0]);
            else
                f[0].setHighPass(fSampleRate, kLowCut[cut]);
            f[1].setHighShelf(fSampleRate, nyquistSafe(9000.0), knob(kStudioDiAir));
            f[2].setLowPass(fSampleRate, nyquistSafe(21000.0), 0.6);
            f[3].setLowPass(fSampleRate, 150.0);
        }
        fIron = knob(kStudioDiIron) / 100.0f;
        fIronDrive = 1.0f + 30.0f * fIron * fIron;
        fLevel = dbToGain(knob(kStudioDiLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        const float in = f[0].process(x);
        // a pushed transformer core saturates at low frequencies first: only the lows go through the curve (and a
        // touch of asymmetry, for the 2nd harmonic)
        const float low = f[3].process(in);
        const float driven = shaper::asymmetric(low * fIronDrive, 1.0f, 0.85f) / fIronDrive;
        const float y = in + (driven - low) * fIron;
        return f[2].process(f[1].process(dcBlock(ch, y))) * fLevel;
    }

private:
    float fIron = 0.0f, fIronDrive = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Valve DI

constexpr const char* kValveCharacterNames[] = { "Flat", "Deep", "Bright", "Mid", "Scoop", "Vintage" };

class ValveDiBlock : public DiBase {
public:
    ValveDiBlock() : DiBase(kValveDiBlockDescriptor) {}

protected:
    // filters: 0 low shelf, 1 mid peak, 2 high shelf, 3 top (each voicing sets them)
    void design() override
    {
        struct Voicing {
            double lowHz, lowDb, midHz, midDb, midQ, highHz, highDb, topHz;
        };
        static constexpr Voicing kVoicings[] = {
            { 80.0, 0.0, 800.0, 0.0, 0.8, 4000.0, 0.0, 20000.0 },   // Flat
            { 80.0, 5.0, 800.0, -2.0, 0.8, 4000.0, 0.0, 20000.0 },  // Deep
            { 100.0, -1.0, 800.0, 0.0, 0.8, 3000.0, 5.0, 20000.0 }, // Bright
            { 80.0, 0.0, 800.0, 5.0, 0.8, 4000.0, 0.0, 20000.0 },   // Mid
            { 80.0, 3.0, 600.0, -6.0, 0.7, 4000.0, 3.0, 20000.0 },  // Scoop
            { 80.0, 0.0, 250.0, 3.0, 0.8, 4000.0, 0.0, 4500.0 },    // Vintage: warm, no top
        };
        const Voicing& v = kVoicings[std::clamp(choice(kValveDiCharacter), 0, 5)];
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setLowShelf(fSampleRate, v.lowHz, v.lowDb);
            f[1].setPeak(fSampleRate, v.midHz, v.midDb, v.midQ);
            f[2].setHighShelf(fSampleRate, v.highHz, v.highDb);
            f[3].setLowPass(fSampleRate, nyquistSafe(v.topHz), 0.7);
        }
        fDrive = dbToGainD(24.0 * knob(kValveDiWarmth) / 100.0);
        fLevel = dbToGain(knob(kValveDiLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        // the tube at unity gain for small signals, rounding off more as Warmth drives it
        const float warm = dcBlock(ch, fOversampler[ch][0].process(x * fDrive, tube)) / fDrive;
        return f[3].process(f[2].process(f[1].process(f[0].process(warm)))) * fLevel;
    }

private:
    float fDrive = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Tube Preamp

constexpr const char* kTubeMidFreqNames[] = { "220 Hz", "450 Hz", "800 Hz", "1.6 kHz", "3 kHz" };

class TubePreampBlock : public DiBase {
public:
    TubePreampBlock() : DiBase(kTubePreampBlockDescriptor) {}

protected:
    // filters: 0 input high-pass, 1 bass, 2 mid, 3 treble, 4-5 Ultra Lo (deep shelf, mid cut), 6 Ultra Hi, 7 top
    void design() override
    {
        static constexpr double kMidHz[] = { 220.0, 450.0, 800.0, 1600.0, 3000.0 };
        const double midHz = kMidHz[std::clamp(choice(kTubePreMidFreq), 0, 4)];
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setHighPass(fSampleRate, 30.0);
            f[1].setLowShelf(fSampleRate, 60.0, knob(kTubePreBass));
            f[2].setPeak(fSampleRate, midHz, knob(kTubePreMid), 0.9);
            f[3].setHighShelf(fSampleRate, 4000.0, knob(kTubePreTreble));
            if (toggle(kTubePreUltraLo))
            {
                f[4].setLowShelf(fSampleRate, 40.0, 2.0);
                f[5].setPeak(fSampleRate, 500.0, -10.0, 0.6);
            }
            else
            {
                flat(f[4]);
                flat(f[5]);
            }
            if (toggle(kTubePreUltraHi))
                f[6].setHighShelf(fSampleRate, nyquistSafe(8000.0), 9.0);
            else
                flat(f[6]);
            f[7].setLowPass(fSampleRate, nyquistSafe(14000.0), 0.7);
        }
        // up to 30 dB into the first tube; a quarter of it shows as level (a quarter in dB), the rest as drive
        fGain = dbToGainD(30.0 * knob(kTubePreGain) / 100.0);
        fOut = std::pow(fGain, -0.75f);
        fLevel = dbToGain(knob(kTubePreLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        const float first = fOut * dcBlock(ch, fOversampler[ch][0].process(f[0].process(x) * fGain, tube));
        float eq = first;
        for (int k = 1; k <= 6; ++k)
            eq = f[k].process(eq);
        // the second stage only rounds off what the tone stack boosted
        const float second = fOversampler[ch][1].process(eq, shaper::soft);
        return f[7].process(second) * fLevel;
    }

private:
    float fGain = 1.0f, fOut = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Driver DI

class DriverDiBlock : public DiBase {
public:
    DriverDiBlock() : DiBase(kDriverDiBlockDescriptor) {}

protected:
    // filters: 0 high-pass into the drive, 1 its mid push, 2-6 the speaker, 7 Presence, 8 Bass, 9 Treble
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setHighPass(fSampleRate, 40.0);
            f[1].setPeak(fSampleRate, 700.0, 4.0, 0.7);
            designSpeaker(ch, 2);
            f[7].setHighShelf(fSampleRate, 2500.0, knob(kDriverPresence));
            f[8].setLowShelf(fSampleRate, 80.0, knob(kDriverBass));
            f[9].setHighShelf(fSampleRate, nyquistSafe(5000.0), knob(kDriverTreble));
        }
        fDrive = dbToGainD(6.0 + 36.0 * knob(kDriverDrive) / 100.0);
        fBlend = knob(kDriverBlend) / 100.0f;
        fLevel = dbToGain(knob(kDriverLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        // the amp: a tube-like stage pushed hard, then its speaker; makes up for the speaker's loss
        const float amp = fOversampler[ch][0].process(f[1].process(f[0].process(x)) * fDrive, [](float v) {
            return tube(shaper::medium(v));
        });
        const float driven = f[7].process(speaker(ch, 2, 0.25f * dcBlock(ch, amp)));
        const float mix = x + (driven - x) * fBlend;
        return f[9].process(f[8].process(mix)) * fLevel;
    }

private:
    float fDrive = 1.0f, fBlend = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Modern Drive

class ModernDriveBlock : public DiBase {
public:
    ModernDriveBlock() : DiBase(kModernDriveBlockDescriptor) {}

protected:
    // filters: 0 high-pass into the drive, 1 Grunt, 2 Attack, 3 top of the drive, 4 Bass, 5 Lo Mid, 6 Hi Mid, 7 Treble
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setHighPass(fSampleRate, 70.0);
            f[1].setLowShelf(fSampleRate, 150.0, -6.0 + 18.0 * knob(kModernGrunt) / 100.0);
            f[2].setHighShelf(fSampleRate, 2500.0, -6.0 + 18.0 * knob(kModernAttack) / 100.0);
            f[3].setLowPass(fSampleRate, nyquistSafe(6500.0), 0.7);
            f[4].setLowShelf(fSampleRate, 80.0, knob(kModernBass));
            f[5].setPeak(fSampleRate, 500.0, knob(kModernLoMid), 1.0);
            f[6].setPeak(fSampleRate, 1500.0, knob(kModernHiMid), 1.0);
            f[7].setHighShelf(fSampleRate, nyquistSafe(5000.0), knob(kModernTreble));
        }
        fDrive = dbToGainD(12.0 + 42.0 * knob(kModernDrive) / 100.0);
        fBlend = knob(kModernBlend) / 100.0f;
        fLevel = dbToGain(knob(kModernLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        const float in = f[2].process(f[1].process(f[0].process(x))) * fDrive;
        const float driven = f[3].process(0.15f * fOversampler[ch][0].process(in, shaper::hard));
        float y = x + (driven - x) * fBlend;
        for (int k = 4; k <= 7; ++k)
            y = f[k].process(y);
        return y * fLevel;
    }

private:
    float fDrive = 1.0f, fBlend = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Crossover Drive

class CrossoverDriveBlock : public DiBase {
public:
    CrossoverDriveBlock() : DiBase(kCrossoverDriveBlockDescriptor) {}

protected:
    // filters: 0-1 the crossover's low-pass and 4-5 its high-pass (Linkwitz-Riley: two Butterworths each, so the
    // bands sum back flat), 2 Tone, 3 high-pass after the drive (keeps its intermodulation out of the clean lows)
    void design() override
    {
        const double xover = knob(kXoverFreq);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setLowPass(fSampleRate, xover);
            f[1].setLowPass(fSampleRate, xover);
            f[4].setHighPass(fSampleRate, xover);
            f[5].setHighPass(fSampleRate, xover);
            f[2].setLowPass(fSampleRate, nyquistSafe(1500.0 * std::pow(8.0, knob(kXoverTone) / 100.0)), 0.7);
            f[3].setHighPass(fSampleRate, 0.7 * xover);
        }
        // 20 dB at 0 (where the band is still about linear and the bands sum flat), up to 50 dB
        fDrive = dbToGainD(20.0 + 30.0 * knob(kXoverDrive) / 100.0);
        fLows = dbToGain(knob(kXoverLows));
        fHighs = dbToGain(knob(kXoverHighs));
        fLevel = dbToGain(knob(kXoverLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        const float low = f[1].process(f[0].process(x));
        const float high = f[5].process(f[4].process(x));
        const float clipped = 0.1f * fOversampler[ch][0].process(high * fDrive, shaper::medium);
        const float driven = f[3].process(f[2].process(clipped));
        return (low * fLows + driven * fHighs) * fLevel;
    }

private:
    float fDrive = 1.0f, fLows = 1.0f, fHighs = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Dual Drive

class DualDriveBlock : public DiBase {
public:
    DualDriveBlock() : DiBase(kDualDriveBlockDescriptor) {}

protected:
    // filters: 0 high-pass into the drive, 1 top of the drive, 2-4 Color (mid cut, bass and treble lift), 5 Bass,
    // 6 Mid, 7 Treble
    void design() override
    {
        const bool color = toggle(kDualColor);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setHighPass(fSampleRate, 100.0);
            f[1].setLowPass(fSampleRate, nyquistSafe(5000.0), 0.7);
            f[2].setPeak(fSampleRate, 500.0, color ? -8.0 : 0.0, 0.7);
            f[3].setLowShelf(fSampleRate, 100.0, color ? 4.0 : 0.0);
            f[4].setHighShelf(fSampleRate, 3000.0, color ? 4.0 : 0.0);
            f[5].setLowShelf(fSampleRate, 80.0, knob(kDualBass));
            f[6].setPeak(fSampleRate, 600.0, knob(kDualMid), 0.8);
            f[7].setHighShelf(fSampleRate, 3500.0, knob(kDualTreble));
        }
        fDrive = dbToGainD(12.0 + 36.0 * knob(kDualDrive) / 100.0);
        fClean = dbToGain(knob(kDualClean));
        fDist = dbToGain(knob(kDualDist));
        fLevel = dbToGain(knob(kDualLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        float dist = f[1].process(0.15f * fOversampler[ch][0].process(f[0].process(x) * fDrive, shaper::hard));
        dist = f[4].process(f[3].process(f[2].process(dist)));
        const float mix = x * fClean + dist * fDist;
        return f[7].process(f[6].process(f[5].process(mix))) * fLevel;
    }

private:
    float fDrive = 1.0f, fClean = 1.0f, fDist = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Hi-Fi Preamp

class HiFiPreampBlock : public DiBase {
public:
    HiFiPreampBlock() : DiBase(kHiFiPreampBlockDescriptor) {}

    float meter() const override { return fMeter; }

protected:
    void prepareBox(double sampleRate) override
    {
        for (OptoCell& cell : fCell)
            cell.prepare(sampleRate);
    }

    void resetBox() override
    {
        for (OptoCell& cell : fCell)
            cell.reset();
        fMeter = 0.0f;
    }

    // filters: 0 input high-pass, 1 Bass, 2 Mid, 3 Treble
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setHighPass(fSampleRate, 25.0);
            f[1].setLowShelf(fSampleRate, 60.0, knob(kHiFiBass));
            f[2].setPeak(fSampleRate, knob(kHiFiMidFreq), knob(kHiFiMid), 0.8);
            f[3].setHighShelf(fSampleRate, 4000.0, knob(kHiFiTreble));
        }
        const float comp = knob(kHiFiComp) / 100.0f;
        fThreshold = -6.0f - 34.0f * comp;
        fCompOn = comp > 0.0f;
        // a little make-up gain as the threshold comes down
        fMakeup = dbToGainD(-0.3 * (6.0 + fThreshold));
        fDrive = dbToGainD(18.0 * knob(kHiFiGain) / 100.0);
        fLevel = dbToGain(knob(kHiFiLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        float in = f[0].process(x);
        if (fCompOn)
        {
            in *= fCell[ch].gain(in, fThreshold, 4.0f) * fMakeup;
            if (ch == 0)
                fMeter = fCell[0].reduction();
        }
        const float warm = dcBlock(ch, fOversampler[ch][0].process(in * fDrive, tube)) / fDrive;
        return f[3].process(f[2].process(f[1].process(warm))) * fLevel;
    }

private:
    OptoCell fCell[2];
    float fThreshold = 0.0f, fMakeup = 1.0f, fDrive = 1.0f, fLevel = 1.0f;
    bool fCompOn = false;
    float fMeter = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// British Preamp

class BritishPreampBlock : public DiBase {
public:
    BritishPreampBlock() : DiBase(kBritishPreampBlockDescriptor) {}

protected:
    // filters: 0 input high-pass, 1-3 Pre Shape (mid cut, low and high lift), 4-8 the graphic, 9 top
    void design() override
    {
        static constexpr double kBandHz[] = { 63.0, 200.0, 600.0, 1600.0, 5000.0 };
        const bool shape = toggle(kBritPreShape);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = fFilters[ch];
            f[0].setHighPass(fSampleRate, 30.0);
            f[1].setPeak(fSampleRate, 400.0, shape ? -10.0 : 0.0, 0.6);
            f[2].setLowShelf(fSampleRate, 60.0, shape ? 3.0 : 0.0);
            f[3].setHighShelf(fSampleRate, 4000.0, shape ? 4.0 : 0.0);
            for (int b = 0; b < 5; ++b)
                f[4 + b].setPeak(fSampleRate, nyquistSafe(kBandHz[b]), knob(kBrit63 + b), 1.1);
            f[9].setLowPass(fSampleRate, nyquistSafe(12000.0), 0.7);
        }
        fDrive = dbToGainD(24.0 * knob(kBritGain) / 100.0);
        // the op-amps' gain shows as level as well as drive: up to about 7 dB louder at full gain
        fMakeup = std::pow(fDrive, -0.7f);
        fLevel = dbToGain(knob(kBritLevel));
    }

    float sample(uint32_t ch, float x) override
    {
        auto& f = fFilters[ch];
        float y = fOversampler[ch][0].process(f[0].process(x) * fDrive, shaper::medium) * fMakeup;
        for (int k = 1; k <= 9; ++k)
            y = f[k].process(y);
        return y * fLevel;
    }

private:
    float fDrive = 1.0f, fMakeup = 1.0f, fLevel = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Opto DI

class OptoDiBlock : public DiBase {
public:
    OptoDiBlock() : DiBase(kOptoDiBlockDescriptor) {}

    float meter() const override { return fMeter; }

protected:
    void prepareBox(double sampleRate) override
    {
        for (OptoCell& cell : fCell)
            cell.prepare(sampleRate);
    }

    void resetBox() override
    {
        for (OptoCell& cell : fCell)
            cell.reset();
        fMeter = 0.0f;
    }

    // filters: 0 Bright
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
            fFilters[ch][0].setHighShelf(fSampleRate, nyquistSafe(6000.0), toggle(kOptoBright) ? 4.0 : 0.0);
        fThreshold = -6.0f - 40.0f * knob(kOptoPeakReduction) / 100.0f;
        fGain = dbToGain(knob(kOptoGain));
        fMix = knob(kOptoMix) / 100.0f;
    }

    float sample(uint32_t ch, float x) override
    {
        // the cell's gentle ratio grows with the level, as an optical cell's does: 3:1 here
        const float compressed = x * fCell[ch].gain(x, fThreshold, 3.0f) * fGain;
        if (ch == 0)
            fMeter = fCell[0].reduction();
        const float y = x + (compressed - x) * fMix;
        // the output stage's transformer and tube, at unity for normal levels
        return fFilters[ch][0].process(dcBlock(ch, fOversampler[ch][0].process(y, tube)));
    }

private:
    OptoCell fCell[2];
    float fThreshold = 0.0f, fGain = 1.0f, fMix = 1.0f;
    float fMeter = 0.0f;
};

} // namespace

const BlockDescriptor kStudioDiBlockDescriptor = {
    "studiodi",
    "Studio DI",
    Category::Di,
    kStudioDiParamCount,
    {
        ParamSpec { "Iron", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Low Cut", "", 0.0f, 3.0f, 0.0f, ParamScale::Choice, kLowCutNames },
        ParamSpec { "Air", "dB", -6.0f, 6.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new StudioDiBlock()); },
};

const BlockDescriptor kValveDiBlockDescriptor = {
    "valvedi",
    "Valve DI",
    Category::Di,
    kValveDiParamCount,
    {
        ParamSpec { "Character", "", 0.0f, 5.0f, 0.0f, ParamScale::Choice, kValveCharacterNames },
        ParamSpec { "Warmth", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ValveDiBlock()); },
};

const BlockDescriptor kTubePreampBlockDescriptor = {
    "tubepreamp",
    "Tube Preamp",
    Category::Di,
    kTubePreParamCount,
    {
        ParamSpec { "Gain", "%", 0.0f, 100.0f, 35.0f, ParamScale::Linear },
        ParamSpec { "Bass", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mid", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mid Freq", "", 0.0f, 4.0f, 1.0f, ParamScale::Choice, kTubeMidFreqNames },
        ParamSpec { "Treble", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Ultra Lo", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Ultra Hi", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new TubePreampBlock()); },
};

const BlockDescriptor kDriverDiBlockDescriptor = {
    "driverdi",
    "Driver DI",
    Category::Di,
    kDriverParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Blend", "%", 0.0f, 100.0f, 70.0f, ParamScale::Linear },
        ParamSpec { "Presence", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Bass", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Treble", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new DriverDiBlock()); },
};

const BlockDescriptor kModernDriveBlockDescriptor = {
    "moderndrive",
    "Modern Drive",
    Category::Di,
    kModernParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Blend", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Grunt", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Attack", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Bass", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Lo Mid", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Hi Mid", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Treble", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ModernDriveBlock()); },
};

const BlockDescriptor kCrossoverDriveBlockDescriptor = {
    "crossoverdrive",
    "Crossover Drive",
    Category::Di,
    kXoverParamCount,
    {
        ParamSpec { "Crossover", "Hz", 80.0f, 500.0f, 220.0f, ParamScale::Log },
        ParamSpec { "Lows", "dB", -24.0f, 6.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Tone", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Highs", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new CrossoverDriveBlock()); },
};

const BlockDescriptor kDualDriveBlockDescriptor = {
    "dualdrive",
    "Dual Drive",
    Category::Di,
    kDualParamCount,
    {
        ParamSpec { "Clean", "dB", -30.0f, 6.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Dist", "dB", -30.0f, 6.0f, -6.0f, ParamScale::Linear },
        ParamSpec { "Color", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Bass", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mid", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Treble", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new DualDriveBlock()); },
};

const BlockDescriptor kHiFiPreampBlockDescriptor = {
    "hifipreamp",
    "Hi-Fi Preamp",
    Category::Di,
    kHiFiParamCount,
    {
        ParamSpec { "Gain", "%", 0.0f, 100.0f, 25.0f, ParamScale::Linear },
        ParamSpec { "Comp", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Bass", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mid", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mid Freq", "Hz", 180.0f, 1000.0f, 400.0f, ParamScale::Log },
        ParamSpec { "Treble", "dB", -15.0f, 15.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new HiFiPreampBlock()); },
    -1, -1, false,
    "Comp",
};

const BlockDescriptor kBritishPreampBlockDescriptor = {
    "britishpreamp",
    "British Preamp",
    Category::Di,
    kBritParamCount,
    {
        ParamSpec { "Gain", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Pre Shape", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "63 Hz", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "200 Hz", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "600 Hz", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "1.6 kHz", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "5 kHz", "dB", -12.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new BritishPreampBlock()); },
};

const BlockDescriptor kOptoDiBlockDescriptor = {
    "optodi",
    "Opto DI",
    Category::Di,
    kOptoParamCount,
    {
        ParamSpec { "Peak Reduction", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Gain", "dB", -12.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Bright", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
    },
    [] { return std::unique_ptr<Block>(new OptoDiBlock()); },
    -1, -1, false,
    "Reduction",
};

} // namespace xythos
