// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/CompBlocks.hpp"

#include "engine/dsp/Biquad.hpp"
#include "engine/dsp/Oversampler.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"
#include "engine/dsp/Waveshapers.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

// Knob changes redesign the filters and coefficients every kUpdateInterval samples while they glide
constexpr uint32_t kUpdateInterval = 32;
constexpr float kLn10Over20 = 0.11512925f;

float levelDb(float x) { return 20.0f * std::log10(std::fabs(x) + 1e-9f); }
float reductionGain(float reductionDb) { return std::exp(-reductionDb * kLn10Over20); }

// How far a level is over the threshold, with a soft knee `knee` dB wide around it (0 below it)
float kneeExcess(float over, float knee)
{
    if (2.0f * over >= knee)
        return over;
    if (knee <= 0.0f || 2.0f * over <= -knee)
        return 0.0f;
    const float t = over + knee / 2.0f;
    return t * t / (2.0f * knee);
}

// One step of a gain reduction (dB) towards its target: attack while it grows, release while it falls
float follow(float value, float target, float attackCoef, float releaseCoef)
{
    return target + (value - target) * (target > value ? attackCoef : releaseCoef);
}

float coef(double sampleRate, double ms) { return EnvelopeFollower::coef(sampleRate, ms); }

// A triode-like stage: the wave flattens sooner on one side (even harmonics); no output for no input
constexpr float kTubeBias = 0.15f;
const float kTubeOffset = shaper::asymmetric(kTubeBias, 1.0f, 0.6f);
float tube(float x) { return shaper::asymmetric(x + kTubeBias, 1.0f, 0.6f) - kTubeOffset; }

// A class-A transistor stage (a Neve preamp's): biased off centre, more headroom than the tube, a gentle 2nd and 3rd
constexpr float kClassABias = 0.1f;
const float kClassAOffset = shaper::asymmetric(kClassABias, 1.2f, 0.8f);
float classA(float x) { return shaper::asymmetric(x + kClassABias, 1.2f, 0.8f) - kClassAOffset; }

// What the compressors share: their knobs (smoothed, in their own units), filters, two oversamplers and a DC blocker
// per channel, and the loop. A compressor implements design() (coefficients and filters from the knobs) and frame()
// (one frame, both channels: the detection is linked), and sets fReduction (dB) in frame() for the meter.
class CompBase : public Block {
public:
    explicit CompBase(const BlockDescriptor& descriptor)
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
        prepareComp(sampleRate);
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
        const int channels = numChannels > 1 ? 2 : 1;
        float maxReduction = 0.0f;
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
            float x[2] = { io[0][i], channels > 1 ? io[1][i] : 0.0f };
            frame(x, channels);
            io[0][i] = x[0];
            if (channels > 1)
                io[1][i] = x[1];
            maxReduction = std::max(maxReduction, fReduction);
        }
        fMeter = maxReduction;
    }

    float meter() const override { return fMeter; }

    void reset() override
    {
        for (auto& pair : fOversampler)
            for (Oversampler4x& os : pair)
                os.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            for (Biquad& filter : fFilters[ch])
                filter.reset();
            fDcBlock[ch].reset();
        }
        resetComp();
        fReduction = fMeter = 0.0f;
        fSnap = true;
    }

protected:
    virtual void prepareComp(double /*sampleRate*/) {}
    virtual void resetComp() {}
    virtual void design() = 0;
    virtual void frame(float* x, int channels) = 0;

    float knob(int index) const { return fKnob[static_cast<size_t>(index)].value(); }
    int choice(int index) const { return static_cast<int>(std::lround(knob(index))); }

    double nyquistSafe(double hz) const { return std::min(hz, 0.45 * fSampleRate); }
    float dcBlock(int ch, float x) { return fDcBlock[ch].highPass(x); }

    // A transformer's iron: only the lows (under the filter `lowPass`, 150 Hz or so) go through a slightly asymmetric
    // curve, and replace the clean lows by `amount`; at unity for small signals
    float iron(int ch, int lowPass, float x, float drive, float amount)
    {
        const float low = fFilters[ch][lowPass].process(x);
        const float driven = shaper::asymmetric(low * drive, 1.0f, 0.85f) / drive;
        return x + (driven - low) * amount;
    }

    double fSampleRate = 48000.0;
    Biquad fFilters[2][6];
    Oversampler4x fOversampler[2][2];
    float fReduction = 0.0f; // dB, set by frame()

private:
    const BlockDescriptor& fDescriptor;
    std::array<Smoother, kParamsPerSlot> fKnob;
    OnePole fDcBlock[2];
    float fMeter = 0.0f;
    uint32_t fCounter = 0;
    bool fDesign = true;
    bool fSnap = true;
};

// The Neve diode-bridge compressor's control side, shared by Diode Bridge and Console Comp. It's a feedback design:
// the detector listens to the bridge's output, so a slope s gives a ratio of 1 + s. The reduction is a fast part and a
// slow part (both the same for the fixed recovery times; the 33609's Auto settings mix a fast and a slow one, so short
// peaks recover quickly and long passages slowly).
class BridgeControl {
public:
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        fDetector.prepare(sampleRate, 0.3, 30.0);
    }

    void reset()
    {
        fDetector.reset();
        fFast = fSlow = 0.0f;
    }

    void setTimes(double attackMs, double fastReleaseMs, double slowAttackMs, double slowReleaseMs, float fastWeight)
    {
        fAttack = coef(fSampleRate, attackMs);
        fFastRelease = coef(fSampleRate, fastReleaseMs);
        fSlowAttack = coef(fSampleRate, slowAttackMs);
        fSlowRelease = coef(fSampleRate, slowReleaseMs);
        fFastWeight = fastWeight;
    }

    // The reduction (dB) for this sample, from the bridge's last output level (linear, the louder channel)
    float reduction(float output, float thresholdDb, float slope, float kneeDb)
    {
        const float env = fDetector.process(output);
        const float target = kneeExcess(levelDb(env) - thresholdDb, kneeDb) * slope;
        fFast = follow(fFast, target, fAttack, fFastRelease);
        fSlow = follow(fSlow, target, fSlowAttack, fSlowRelease);
        return std::max(fFastWeight * fFast + (1.0f - fFastWeight) * fSlow, 0.0f);
    }

private:
    double fSampleRate = 48000.0;
    EnvelopeFollower fDetector;
    float fAttack = 0.0f, fFastRelease = 0.0f, fSlowAttack = 0.0f, fSlowRelease = 0.0f;
    float fFastWeight = 1.0f;
    float fFast = 0.0f, fSlow = 0.0f;
};

// The diode bridge passes the signal through its diodes, unbalanced a little: an even distortion (mostly the 2nd
// harmonic) that grows with the gain reduction, about 0.5% at 10 dB. The bridge's output sits near the threshold while
// it works, so the curve is scaled to the threshold's level (`reference`); bounded, for any level.
float bridgeColour(float x, float reductionDb, float reference)
{
    const float u = x / reference;
    const float even = u * u / (1.0f + u * u);
    return x + 0.0017f * std::min(reductionDb, 24.0f) * reference * even;
}

constexpr float kBridgeRatios[] = { 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
constexpr const char* kBridgeRatioNames[] = { "1.5:1", "2:1", "3:1", "4:1", "6:1" };

// ---------------------------------------------------------------------------------------------------------------------
// Diode Bridge

constexpr const char* kRecoveryNames[] = { "100 ms", "400 ms", "800 ms", "1.5 s", "Auto 1", "Auto 2" };

class DiodeBridgeBlock : public CompBase {
public:
    DiodeBridgeBlock() : CompBase(kDiodeBridgeBlockDescriptor) {}

protected:
    void prepareComp(double sampleRate) override
    {
        fControl.prepare(sampleRate);
        fLimitAttack = coef(sampleRate, 1.0);
        fLimitRelease = coef(sampleRate, 100.0);
    }

    void resetComp() override
    {
        fControl.reset();
        fPrev[0] = fPrev[1] = 0.0f;
        fLimit = 0.0f;
    }

    // filters: 0 the input transformer's lows, 1 the output transformer's top
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fFilters[ch][0].setLowPass(fSampleRate, 150.0);
            fFilters[ch][1].setLowPass(fSampleRate, nyquistSafe(22000.0), 0.6);
        }
        static constexpr double kRecovery[] = { 100.0, 400.0, 800.0, 1500.0 };
        const int recovery = std::clamp(choice(kBridgeRecovery), 0, 5);
        if (recovery == 4) // Auto 1: fast peaks 100 ms, long passages 1.5 s
            fControl.setTimes(4.0, 100.0, 300.0, 1500.0, 0.6f);
        else if (recovery == 5) // Auto 2: 200 ms and 5 s
            fControl.setTimes(4.0, 200.0, 600.0, 5000.0, 0.5f);
        else
            fControl.setTimes(4.0, kRecovery[recovery], 4.0, kRecovery[recovery], 1.0f);
        fThreshold = knob(kBridgeThreshold);
        fReference = dbToGain(fThreshold);
        fSlope = kBridgeRatios[std::clamp(choice(kBridgeRatio), 0, 4)] - 1.0f;
        fLimitThreshold = knob(kBridgeLimit);
        fGain = dbToGain(knob(kBridgeGain));
        fMix = knob(kBridgeMix) / 100.0f;
    }

    void frame(float* x, int channels) override
    {
        const float fed = channels > 1 ? std::max(std::fabs(fPrev[0]), std::fabs(fPrev[1])) : std::fabs(fPrev[0]);
        const float reduction = fControl.reduction(fed, fThreshold, fSlope, 6.0f);
        const float gain = reductionGain(reduction);

        float wet[2];
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float in = iron(ch, 0, x[ch], 2.0f, 0.5f);
            const float bridged = dcBlock(ch, bridgeColour(in * gain, reduction, fReference));
            fPrev[ch] = bridged;
            wet[ch] = fFilters[ch][1].process(bridged) * fGain;
            peak = std::max(peak, std::fabs(wet[ch]));
        }
        // the limiter after it: fast, 20:1, feed-forward
        const float limitTarget = kneeExcess(levelDb(peak) - fLimitThreshold, 2.0f) * (1.0f - 1.0f / 20.0f);
        fLimit = follow(fLimit, limitTarget, fLimitAttack, fLimitRelease);
        const float limitGain = reductionGain(fLimit);
        for (int ch = 0; ch < channels; ++ch)
            x[ch] += (wet[ch] * limitGain - x[ch]) * fMix;
        fReduction = reduction + fLimit;
    }

private:
    BridgeControl fControl;
    float fPrev[2] = { 0.0f, 0.0f };
    float fThreshold = -20.0f, fReference = 0.1f, fSlope = 1.0f, fLimitThreshold = 0.0f, fGain = 1.0f, fMix = 1.0f;
    float fLimit = 0.0f, fLimitAttack = 0.0f, fLimitRelease = 0.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Console Comp

constexpr const char* kConsoleAttackNames[] = { "Fast", "Medium", "Slow" };

class ConsoleCompBlock : public CompBase {
public:
    ConsoleCompBlock() : CompBase(kConsoleCompBlockDescriptor) {}

protected:
    void prepareComp(double sampleRate) override { fControl.prepare(sampleRate); }

    void resetComp() override
    {
        fControl.reset();
        fPrev[0] = fPrev[1] = 0.0f;
    }

    // filters: 0 the input transformer's lows, 1 the output transformer's top
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fFilters[ch][0].setLowPass(fSampleRate, 120.0);
            fFilters[ch][1].setLowPass(fSampleRate, nyquistSafe(20000.0), 0.6);
        }
        static constexpr double kAttack[] = { 2.0, 8.0, 25.0 };
        const double release = knob(kConsoleRelease);
        fControl.setTimes(kAttack[std::clamp(choice(kConsoleAttack), 0, 2)], release, 1.0, release, 1.0f);
        const float drive = knob(kConsoleDrive) / 100.0f;
        // from 12 dB under to 24 dB over unity into the preamp, scaled back so a quarter of the rise (in dB) shows as
        // level
        fDrive = dbToGain(-12.0f + 36.0f * drive);
        fDriveBack = dbToGain(12.0f - 27.0f * drive);
        fIronDrive = 2.0f + 6.0f * drive;
        fIronAmount = 0.3f + 0.7f * drive;
        fThreshold = knob(kConsoleThreshold);
        fReference = dbToGain(fThreshold);
        fSlope = kBridgeRatios[std::clamp(choice(kConsoleRatio), 0, 4)] - 1.0f;
        fLevel = dbToGain(knob(kConsoleLevel));
        fMix = knob(kConsoleMix) / 100.0f;
    }

    void frame(float* x, int channels) override
    {
        const float fed = channels > 1 ? std::max(std::fabs(fPrev[0]), std::fabs(fPrev[1])) : std::fabs(fPrev[0]);
        const float reduction = fControl.reduction(fed, fThreshold, fSlope, 6.0f);
        const float gain = reductionGain(reduction);
        for (int ch = 0; ch < channels; ++ch)
        {
            // the preamp: the class-A stage, its input transformer's iron
            const float driven = fOversampler[ch][0].process(x[ch] * fDrive, classA) * fDriveBack;
            const float pre = dcBlock(ch, iron(ch, 0, driven, fIronDrive, fIronAmount));
            // the compressor's bridge
            const float bridged = bridgeColour(pre * gain, reduction, fReference);
            fPrev[ch] = bridged;
            const float wet = fFilters[ch][1].process(bridged) * fLevel;
            x[ch] += (wet - x[ch]) * fMix;
        }
        fReduction = reduction;
    }

private:
    BridgeControl fControl;
    float fPrev[2] = { 0.0f, 0.0f };
    float fDrive = 1.0f, fDriveBack = 1.0f, fIronDrive = 2.0f, fIronAmount = 0.3f;
    float fThreshold = -20.0f, fReference = 0.1f, fSlope = 2.0f, fLevel = 1.0f, fMix = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Opto Leveler

constexpr const char* kLevelerModeNames[] = { "Compress", "Limit" };

class OptoLevelerBlock : public CompBase {
public:
    OptoLevelerBlock() : CompBase(kOptoLevelerBlockDescriptor) {}

protected:
    void prepareComp(double sampleRate) override
    {
        fDetector.prepare(sampleRate, 0.5, 25.0);
        fAttack = coef(sampleRate, 10.0);
        fFastRelease = coef(sampleRate, 60.0);
        fHistoryCoef = coef(sampleRate, 3000.0);
        fSamplesPerMs = static_cast<float>(sampleRate / 1000.0);
    }

    void resetComp() override
    {
        fDetector.reset();
        fFast = fSlow = fHistory = 0.0f;
        fPrev[0] = fPrev[1] = 0.0f;
    }

    // filters: 0 the side chain's emphasis (it listens to the output)
    void design() override
    {
        const double emphasis = 12.0 * knob(kLevelerEmphasis) / 100.0;
        for (int ch = 0; ch < 2; ++ch)
            fFilters[ch][0].setHighShelf(fSampleRate, 2000.0, emphasis);
        fThreshold = -6.0f - 40.0f * knob(kLevelerPeakReduction) / 100.0f;
        const bool limit = choice(kLevelerMode) == 1;
        fSlope = limit ? 9.0f : 2.0f; // feedback: about 10:1 and 3:1 well over the threshold
        fKnee = limit ? 4.0f : 10.0f;
        fGain = dbToGain(knob(kLevelerGain));
        fMix = knob(kLevelerMix) / 100.0f;
    }

    void frame(float* x, int channels) override
    {
        float fed = std::fabs(fFilters[0][0].process(fPrev[0]));
        if (channels > 1)
            fed = std::max(fed, std::fabs(fFilters[1][0].process(fPrev[1])));
        const float env = fDetector.process(fed);
        const float target = kneeExcess(levelDb(env) - fThreshold, fKnee) * fSlope;

        // The cell attacks in about 10 ms. Half of it lets go in about 60 ms, the other half in 0.5 to 5 s: the
        // longer and harder it has been working (its history), the slower.
        fHistory = fReduction + (fHistory - fReduction) * fHistoryCoef;
        const float slowMs = 500.0f + 4500.0f * std::clamp(fHistory / 12.0f, 0.0f, 1.0f);
        const float slowRelease = std::exp(-1.0f / (slowMs * fSamplesPerMs));
        fFast = follow(fFast, target, fAttack, fFastRelease);
        fSlow = follow(fSlow, target, fAttack, slowRelease);
        const float reduction = std::max(0.5f * (fFast + fSlow), 0.0f);
        const float gain = reductionGain(reduction);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float cell = x[ch] * gain;
            fPrev[ch] = cell;
            // the gain stage into the output tube
            const float wet = dcBlock(ch, fOversampler[ch][0].process(cell * fGain, tube));
            x[ch] += (wet - x[ch]) * fMix;
        }
        fReduction = reduction;
    }

private:
    EnvelopeFollower fDetector;
    float fAttack = 0.0f, fFastRelease = 0.0f, fHistoryCoef = 0.0f, fSamplesPerMs = 48.0f;
    float fFast = 0.0f, fSlow = 0.0f, fHistory = 0.0f;
    float fPrev[2] = { 0.0f, 0.0f };
    float fThreshold = -22.0f, fSlope = 2.0f, fKnee = 10.0f, fGain = 1.0f, fMix = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// Opto Stomp

class OptoStompBlock : public CompBase {
public:
    OptoStompBlock() : CompBase(kOptoStompBlockDescriptor) {}

protected:
    void prepareComp(double sampleRate) override
    {
        fLed.prepare(sampleRate, 1.0, 15.0);
        fCellAttack = 1.0f - coef(sampleRate, 6.0);
        fPerSecond = static_cast<float>(1.0 / sampleRate);
    }

    void resetComp() override
    {
        fLed.reset();
        fConductance = 0.0f;
    }

    // filters: 0 Tone
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
            fFilters[ch][0].setHighShelf(fSampleRate, nyquistSafe(2500.0), knob(kOptoStompTone));
        const float sustainDb = 40.0f * knob(kOptoStompSustain) / 100.0f;
        fLedDrive = dbToGain(sustainDb);
        fMakeup = dbToGain(0.3f * sustainDb); // a little automatic make-up
        fLevel = dbToGain(knob(kOptoStompLevel));
        fBlend = knob(kOptoStompBlend) / 100.0f;
    }

    void frame(float* x, int channels) override
    {
        const float in = channels > 1 ? std::max(std::fabs(x[0]), std::fabs(x[1])) : std::fabs(x[0]);
        const float light = fLed.process(in) * fLedDrive;
        // the photoresistor's conductance for this much light: it grows a little slower than the light, so the ratio
        // rises gently with the level (about 3:1 when it's working hard)
        const float target = light > 1e-6f ? std::exp(0.7f * std::log(light)) : 0.0f;
        if (target > fConductance)
        {
            fConductance += (target - fConductance) * fCellAttack;
        }
        else
        {
            // it recovers quickly while bright, then slower and slower as it darkens
            const float tau = 0.06f + 0.6f / (1.0f + fConductance);
            fConductance += (target - fConductance) * std::min(fPerSecond / tau, 1.0f);
        }
        // the cell shunts the signal: a divider
        const float gain = fMakeup / (1.0f + fConductance);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float wet = fFilters[ch][0].process(x[ch] * gain);
            x[ch] = (x[ch] + (wet - x[ch]) * fBlend) * fLevel;
        }
        fReduction = 20.0f * std::log10(1.0f + fConductance);
    }

private:
    EnvelopeFollower fLed;
    float fCellAttack = 0.0f, fPerSecond = 0.0f;
    float fConductance = 0.0f;
    float fLedDrive = 1.0f, fMakeup = 1.0f, fLevel = 1.0f, fBlend = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------
// FET Limiter

constexpr const char* kFetRatioNames[] = { "4:1", "8:1", "12:1", "20:1", "All" };
constexpr float kFetThreshold = -10.0f; // dBFS, after Input

class FetLimiterBlock : public CompBase {
public:
    FetLimiterBlock() : CompBase(kFetLimiterBlockDescriptor) {}

protected:
    void resetComp() override { fGr = 0.0f; }

    void design() override
    {
        const int ratio = std::clamp(choice(kFetRatio), 0, 4);
        static constexpr float kRatios[] = { 4.0f, 8.0f, 12.0f, 20.0f };
        static constexpr float kKnees[] = { 4.0f, 3.0f, 2.0f, 1.0f };
        fAll = ratio == 4;
        // all buttons in: past the threshold the output falls again (it over-compresses), and it distorts more
        fSlope = fAll ? 1.1f : 1.0f - 1.0f / kRatios[ratio];
        fKnee = fAll ? 0.0f : kKnees[ratio];
        fFetAmount = fAll ? 0.6f : 0.3f;
        fAttack = coef(fSampleRate, knob(kFetAttack));
        fRelease = coef(fSampleRate, knob(kFetRelease) * (fAll ? 0.7 : 1.0));
        const float input = knob(kFetInput);
        fInput = dbToGain(input);
        fOutput = dbToGain(knob(kFetOutput) - input); // the level stays put as Input pushes it harder
        fMix = knob(kFetMix) / 100.0f;
    }

    void frame(float* x, int channels) override
    {
        const float peak = (channels > 1 ? std::max(std::fabs(x[0]), std::fabs(x[1])) : std::fabs(x[0])) * fInput;
        const float target = kneeExcess(levelDb(peak) - kFetThreshold, fKnee) * fSlope;
        fGr = std::max(follow(fGr, target, fAttack, fRelease), 0.0f);
        const float gain = reductionGain(fGr);
        // the FET (as a variable resistor it adds a 2nd harmonic as it works) and the class-A output amplifier (it
        // clips about 6 dB over full scale), which grits up when Input pushes it
        const float fet = std::min(fGr / 20.0f, 1.0f) * fFetAmount;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float y = fOversampler[ch][0].process(x[ch] * fInput * gain, [fet](float v) {
                const float w = v + fet * (shaper::asymmetric(v * 3.0f, 1.0f, 0.75f) / 3.0f - v);
                return shaper::asymmetric(w, 2.0f, 1.6f);
            });
            const float wet = dcBlock(ch, y) * fOutput;
            x[ch] += (wet - x[ch]) * fMix;
        }
        fReduction = fGr;
    }

private:
    float fGr = 0.0f;
    float fSlope = 0.75f, fKnee = 4.0f, fFetAmount = 0.3f, fAttack = 0.0f, fRelease = 0.0f;
    float fInput = 1.0f, fOutput = 1.0f, fMix = 1.0f;
    bool fAll = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// Squash Comp

constexpr const char* kSquashAttackNames[] = { "Fast", "Slow" };
constexpr float kSquashCeiling = 0.1f; // where loud notes are held (-20 dBFS), before the OTA's make-up

class SquashCompBlock : public CompBase {
public:
    SquashCompBlock() : CompBase(kSquashCompBlockDescriptor) {}

protected:
    void resetComp() override { fEnv.reset(); }

    // filters: 0 the OTA circuit's darker top
    void design() override
    {
        for (int ch = 0; ch < 2; ++ch)
            fFilters[ch][0].setLowPass(fSampleRate, nyquistSafe(6500.0), 0.7);
        fEnv.prepare(fSampleRate, choice(kSquashAttack) == 1 ? 12.0 : 1.0, 250.0);
        fMaxGain = dbToGain(36.0f * knob(kSquashSustain) / 100.0f);
        fLevel = dbToGain(knob(kSquashLevel));
        fBlend = knob(kSquashBlend) / 100.0f;
    }

    void frame(float* x, int channels) override
    {
        const float in = channels > 1 ? std::max(std::fabs(x[0]), std::fabs(x[1])) : std::fabs(x[0]);
        // the OTA's gain falls as the envelope rises: quiet notes come up by up to Sustain, loud ones level off
        const float squeeze = 1.0f + fMaxGain * fEnv.process(in) / kSquashCeiling;
        const float gain = fMaxGain / squeeze;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float ota = fOversampler[ch][0].process(x[ch] * gain * 1.4f, shaper::soft) / 1.4f;
            const float wet = fFilters[ch][0].process(ota);
            x[ch] = (x[ch] + (wet - x[ch]) * fBlend) * fLevel;
        }
        fReduction = 20.0f * std::log10(squeeze);
    }

private:
    EnvelopeFollower fEnv;
    float fMaxGain = 1.0f, fLevel = 1.0f, fBlend = 1.0f;
};

} // namespace

const BlockDescriptor kDiodeBridgeBlockDescriptor = {
    "diodebridge",
    "Diode Bridge",
    Category::Dynamics,
    kBridgeParamCount,
    {
        ParamSpec { "Threshold", "dB", -40.0f, 0.0f, -20.0f, ParamScale::Linear },
        ParamSpec { "Ratio", "", 0.0f, 4.0f, 1.0f, ParamScale::Choice, kBridgeRatioNames },
        ParamSpec { "Recovery", "", 0.0f, 5.0f, 4.0f, ParamScale::Choice, kRecoveryNames },
        ParamSpec { "Limit", "dB", -24.0f, 0.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Gain", "dB", -6.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new DiodeBridgeBlock()); },
    -1, -1, false,
    "Reduction",
};

const BlockDescriptor kConsoleCompBlockDescriptor = {
    "consolecomp",
    "Console Comp",
    Category::Dynamics,
    kConsoleParamCount,
    {
        ParamSpec { "Drive", "%", 0.0f, 100.0f, 20.0f, ParamScale::Linear },
        ParamSpec { "Threshold", "dB", -40.0f, 0.0f, -20.0f, ParamScale::Linear },
        ParamSpec { "Ratio", "", 0.0f, 4.0f, 2.0f, ParamScale::Choice, kBridgeRatioNames },
        ParamSpec { "Attack", "", 0.0f, 2.0f, 1.0f, ParamScale::Choice, kConsoleAttackNames },
        ParamSpec { "Release", "ms", 50.0f, 3000.0f, 400.0f, ParamScale::Log },
        ParamSpec { "Level", "dB", -24.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new ConsoleCompBlock()); },
    -1, -1, false,
    "Reduction",
};

const BlockDescriptor kOptoLevelerBlockDescriptor = {
    "optoleveler",
    "Opto Leveler",
    Category::Dynamics,
    kLevelerParamCount,
    {
        ParamSpec { "Peak Reduction", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
        ParamSpec { "Gain", "dB", -12.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mode", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kLevelerModeNames },
        ParamSpec { "Emphasis", "%", 0.0f, 100.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new OptoLevelerBlock()); },
    -1, -1, false,
    "Reduction",
};

const BlockDescriptor kOptoStompBlockDescriptor = {
    "optostomp",
    "Opto Stomp",
    Category::Dynamics,
    kOptoStompParamCount,
    {
        ParamSpec { "Sustain", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Tone", "dB", -6.0f, 6.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Blend", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new OptoStompBlock()); },
    -1, -1, false,
    "Reduction",
};

const BlockDescriptor kFetLimiterBlockDescriptor = {
    "fetlimiter",
    "FET Limiter",
    Category::Dynamics,
    kFetParamCount,
    {
        ParamSpec { "Input", "dB", 0.0f, 48.0f, 15.0f, ParamScale::Linear },
        ParamSpec { "Output", "dB", -24.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Attack", "ms", 0.02f, 0.8f, 0.3f, ParamScale::Log },
        ParamSpec { "Release", "ms", 50.0f, 1100.0f, 300.0f, ParamScale::Log },
        ParamSpec { "Ratio", "", 0.0f, 4.0f, 0.0f, ParamScale::Choice, kFetRatioNames },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new FetLimiterBlock()); },
    -1, -1, false,
    "Reduction",
};

const BlockDescriptor kSquashCompBlockDescriptor = {
    "squashcomp",
    "Squash Comp",
    Category::Dynamics,
    kSquashParamCount,
    {
        ParamSpec { "Sustain", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Attack", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kSquashAttackNames },
        ParamSpec { "Level", "dB", -24.0f, 12.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Blend", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SquashCompBlock()); },
    -1, -1, false,
    "Reduction",
    36.0f,
};

} // namespace namsayin
