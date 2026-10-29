// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/RhythmBlocks.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

constexpr double kMinFadeMs = 1.0; // the shortest edge: anything faster clicks

double syncedMs(const ParamSpec& spec, float ms, bool sync, double bpm)
{
    return sync ? noteBeats(timeNoteIndex(spec.toNormalized(ms))) * 60000.0 / bpm : ms;
}

double syncedBeats(const ParamSpec& spec, float ms)
{
    return noteBeats(timeNoteIndex(spec.toNormalized(ms)));
}

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kStepCountNames[] = { "2", "3", "4", "5", "6", "7", "8" };
constexpr float kDefaultSteps[kSlicerSteps] = { 100.0f, 0.0f, 100.0f, 0.0f, 100.0f, 100.0f, 0.0f, 100.0f };

class SlicerBlock : public Block {
public:
    SlicerBlock()
    {
        for (int s = 0; s < kSlicerSteps; ++s)
            fLevels[s] = kDefaultSteps[s] / 100.0f;
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        updateTime();
        reset();
    }

    void setParam(int index, float value) override
    {
        if (index >= kSlicerParamStep1 && index < kSlicerParamStep1 + kSlicerSteps)
        {
            fLevels[index - kSlicerParamStep1] = value / 100.0f;
            return;
        }
        switch (index)
        {
        case kSlicerParamSteps: fSteps = std::clamp(static_cast<int>(value), 0, kSlicerSteps - 2) + 2; break;
        case kSlicerParamTime:
            fTimeMs = value;
            updateTime();
            break;
        case kSlicerParamSync:
            fSync = value >= 0.5f;
            updateTime();
            break;
        case kSlicerParamSmooth:
            fSmooth = value / 100.0f;
            updateTime();
            break;
        case kSlicerParamDepth: fDepth = value / 100.0f; break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateTime();
        }
    }

    void setSongPosition(double beats) override
    {
        // synced: the pattern counts from the song's start, so step 1 lands on the bar
        if (fSync)
        {
            const double steps = beats / syncedBeats(kSlicerBlockDescriptor.params[kSlicerParamTime], fTimeMs);
            fPosition = steps - std::floor(steps / fSteps) * fSteps;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            fPosition += fStepAdvance;
            if (fPosition >= fSteps)
                fPosition -= fSteps * std::floor(fPosition / fSteps);
            const int step = std::clamp(static_cast<int>(fPosition), 0, kSlicerSteps - 1);
            const float target = 1.0f - fDepth * (1.0f - fLevels[step]);
            fGain = target + (fGain - target) * fCoef;
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= fGain;
        }
    }

    void reset() override
    {
        fPosition = 0.0;
        fGain = 1.0f - fDepth * (1.0f - fLevels[0]);
    }

private:
    void updateTime()
    {
        const double ms = std::max(1.0, syncedMs(kSlicerBlockDescriptor.params[kSlicerParamTime], fTimeMs, fSync, fBpm));
        fStepAdvance = 1000.0 / (ms * fSampleRate);
        // Smooth: from a hard edge (1 ms) to gliding over most of a step
        const double fadeMs = std::max(kMinFadeMs, 0.3 * ms * fSmooth);
        fCoef = EnvelopeFollower::coef(fSampleRate, fadeMs);
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fLevels[kSlicerSteps] {};
    int fSteps = 8;
    float fTimeMs = 125.0f;
    bool fSync = false;
    float fSmooth = 0.2f;
    float fDepth = 1.0f;
    double fStepAdvance = 0.0;
    double fPosition = 0.0;
    float fCoef = 0.0f;
    float fGain = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kRateModNames[] = { "Off", "Steps", "Sweep" };
constexpr const char* kSpeedNames[] = { "2", "3", "4", "5" };
constexpr const char* kStepLenNames[] = { "1", "2", "4", "8" };
constexpr const char* kDirectionNames[] = { "Faster", "Slower", "Both" };

class KillSwitchBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        updateTime();
        updateFade();
        reset();
    }

    void setParam(int index, float value) override
    {
        const bool on = value >= 0.5f;
        switch (index)
        {
        case kKillParamKill:
            press(fKill, on);
            break;
        case kKillParamStutter:
            press(fStutter, on);
            break;
        case kKillParamLatch:
            fLatch = on;
            if (!fLatch)
                fKill.latched = fStutter.latched = false;
            break;
        case kKillParamInvert: fInvert = on; break;
        case kKillParamTime:
            fTimeMs = value;
            updateTime();
            break;
        case kKillParamSync:
            fSync = on;
            updateTime();
            break;
        case kKillParamDuty: fDuty = value / 100.0f; break;
        case kKillParamFade:
            fFadeMs = value;
            updateFade();
            break;
        case kKillParamRateMod:   fRateMod = std::clamp(static_cast<int>(value), 0, 2); break;
        case kKillParamSpeeds:    fSpeeds = std::clamp(static_cast<int>(value), 0, 3) + 2; break;
        case kKillParamStepLen:   fStepLen = 1 << std::clamp(static_cast<int>(value), 0, 3); break;
        case kKillParamDirection: fDirection = std::clamp(static_cast<int>(value), 0, 2); break;
        }
    }

    void setTempo(double bpm) override
    {
        if (bpm != fBpm)
        {
            fBpm = bpm;
            updateTime();
        }
    }

    void setSongPosition(double beats) override
    {
        // synced: the chops (and the speed steps) land on the beat
        if (fSync)
            fCycles = beats / syncedBeats(kKillSwitchBlockDescriptor.params[kKillParamTime], fTimeMs);
        fSongPlaying = true;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        const bool killed = active(fKill) != fInvert;
        const bool stutter = active(fStutter);
        // free running, a stutter starts its cycle at the press
        if (stutter && !fWasStuttering)
        {
            if (!(fSync && fSongPlaying))
                fCycles = 0.0;
            fSweepPhase = 0.0;
        }
        fWasStuttering = stutter;
        fSongPlaying = false;

        for (uint32_t i = 0; i < frames; ++i)
        {
            fCycles += fPhaseStep;
            float target = killed ? 0.0f : 1.0f;
            if (stutter && phase() >= fDuty)
                target = 0.0f;
            fGain = target + (fGain - target) * fCoef;
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= fGain;
        }
    }

    void reset() override
    {
        fCycles = fSweepPhase = 0.0;
        fGain = active(fKill) != fInvert ? 0.0f : 1.0f;
        fWasStuttering = false;
    }

private:
    struct Switch {
        bool held = false;
        bool latched = false;
    };

    void press(Switch& s, bool on)
    {
        if (on && !s.held && fLatch)
            s.latched = !s.latched;
        s.held = on;
    }

    bool active(const Switch& s) const { return fLatch ? s.latched : s.held; }

    // Where in its on-off cycle the stutter is (0..1), from fCycles: the cycles of Time since it started
    double phase()
    {
        switch (fRateMod)
        {
        case 1: {
            // Steps: Step Len cycles of Time at each speed, then twice as fast; the cycles line up at each step
            const auto step = static_cast<int64_t>(std::floor(fCycles / fStepLen));
            const double within = fCycles - static_cast<double>(step) * fStepLen;
            const double local = within * std::exp2(speedIndex(step));
            return local - std::floor(local);
        }
        case 2: {
            // Sweep: the speed glides from one end to the other over as long as the steps take
            const double length = static_cast<double>(fStepLen) * fSpeeds;
            double u = fCycles / (fDirection == 2 ? 2.0 * length : length);
            u -= std::floor(u);
            double position = fDirection == 0 ? u : (fDirection == 1 ? 1.0 - u : 1.0 - std::fabs(2.0 * u - 1.0));
            fSweepPhase += fPhaseStep * std::exp2(position * (fSpeeds - 1));
            fSweepPhase -= std::floor(fSweepPhase);
            return fSweepPhase;
        }
        default:
            return fCycles - std::floor(fCycles);
        }
    }

    // The speed (as a power of two) of a step: up, down, or up and back down
    int speedIndex(int64_t step) const
    {
        const int n = fSpeeds;
        if (fDirection == 2)
        {
            const int period = 2 * (n - 1);
            const auto p = static_cast<int>(((step % period) + period) % period);
            return p < n ? p : period - p;
        }
        const auto k = static_cast<int>(((step % n) + n) % n);
        return fDirection == 0 ? k : n - 1 - k;
    }

    void updateTime()
    {
        const double ms = std::max(1.0, syncedMs(kKillSwitchBlockDescriptor.params[kKillParamTime], fTimeMs, fSync, fBpm));
        fPhaseStep = 1000.0 / (ms * fSampleRate);
    }

    void updateFade() { fCoef = EnvelopeFollower::coef(fSampleRate, std::max<double>(kMinFadeMs / 3.0, fFadeMs / 3.0)); }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    Switch fKill, fStutter;
    bool fLatch = false;
    bool fInvert = false;
    float fTimeMs = 125.0f;
    bool fSync = false;
    float fDuty = 0.5f;
    float fFadeMs = 2.0f;
    int fRateMod = 0;
    int fSpeeds = 3;
    int fStepLen = 2;
    int fDirection = 0;
    double fPhaseStep = 0.0; // cycles of Time per sample
    double fCycles = 0.0;
    double fSweepPhase = 0.0;
    float fCoef = 0.0f;
    float fGain = 1.0f;
    bool fWasStuttering = false;
    bool fSongPlaying = false;
};

} // namespace

const BlockDescriptor kSlicerBlockDescriptor = {
    "slicer",
    "Slicer",
    Category::Modulation,
    kSlicerParamCount,
    {
        ParamSpec { "Step 1", "%", 0.0f, 100.0f, kDefaultSteps[0], ParamScale::Linear },
        ParamSpec { "Step 2", "%", 0.0f, 100.0f, kDefaultSteps[1], ParamScale::Linear },
        ParamSpec { "Step 3", "%", 0.0f, 100.0f, kDefaultSteps[2], ParamScale::Linear },
        ParamSpec { "Step 4", "%", 0.0f, 100.0f, kDefaultSteps[3], ParamScale::Linear },
        ParamSpec { "Step 5", "%", 0.0f, 100.0f, kDefaultSteps[4], ParamScale::Linear },
        ParamSpec { "Step 6", "%", 0.0f, 100.0f, kDefaultSteps[5], ParamScale::Linear },
        ParamSpec { "Step 7", "%", 0.0f, 100.0f, kDefaultSteps[6], ParamScale::Linear },
        ParamSpec { "Step 8", "%", 0.0f, 100.0f, kDefaultSteps[7], ParamScale::Linear },
        ParamSpec { "Steps", "", 0.0f, static_cast<float>(kSlicerSteps - 2), static_cast<float>(kSlicerSteps - 2),
                    ParamScale::Choice, kStepCountNames },
        ParamSpec { "Time", "ms", 20.0f, 1000.0f, 125.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Smooth", "%", 0.0f, 100.0f, 20.0f, ParamScale::Linear },
        ParamSpec { "Depth", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SlicerBlock()); },
    kSlicerParamSync,
    kSlicerParamTime,
    false,
};

const BlockDescriptor kKillSwitchBlockDescriptor = {
    "killswitch",
    "Kill Switch",
    Category::Dynamics,
    kKillParamCount,
    {
        ParamSpec { "Kill", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Stutter", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Latch", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Invert", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Time", "ms", 20.0f, 1000.0f, 125.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Duty", "%", 5.0f, 95.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Fade", "ms", 1.0f, 20.0f, 2.0f, ParamScale::Log },
        ParamSpec { "Rate Mod", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kRateModNames },
        ParamSpec { "Speeds", "", 0.0f, 3.0f, 1.0f, ParamScale::Choice, kSpeedNames },
        ParamSpec { "Step Len", "", 0.0f, 3.0f, 1.0f, ParamScale::Choice, kStepLenNames },
        ParamSpec { "Direction", "", 0.0f, 2.0f, 0.0f, ParamScale::Choice, kDirectionNames },
    },
    [] { return std::unique_ptr<Block>(new KillSwitchBlock()); },
    kKillParamSync,
    kKillParamTime,
    false,
};

} // namespace xythos
