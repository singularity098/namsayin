// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/TimeFxBlocks.hpp"

#include "engine/TempoSync.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/PitchShifter.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace xythos {

namespace {

// A time knob in ms, or (synced) the note value the same knob picks, in ms at `bpm`
double syncedMs(const ParamSpec& spec, float ms, bool sync, double bpm)
{
    return sync ? noteBeats(timeNoteIndex(spec.toNormalized(ms))) * 60000.0 / bpm : ms;
}

constexpr double kTapeBufferSeconds = 5.0;
constexpr float kTapeFadeSpeed = 0.04f; // below this speed the sound fades out (a stopped reel is silent)
constexpr double kTapeEdgeMs = 5.0;      // the crossfade back to the live signal

class TapeStopBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(kTapeBufferSeconds * sampleRate));
        fEdgeStep = static_cast<float>(1000.0 / (kTapeEdgeMs * sampleRate));
        reset();
    }

    void setParam(int index, float value) override
    {
        const bool on = value >= 0.5f;
        switch (index)
        {
        case kTapeStopParamStop:
            if (on && !fHeld && fLatch)
                fLatched = !fLatched;
            fHeld = on;
            break;
        case kTapeStopParamLatch:
            if (fLatch && !on)
                fLatched = false;
            fLatch = on;
            break;
        case kTapeStopParamStopTime:  fStopMs = value; break;
        case kTapeStopParamStartTime: fStartMs = value; break;
        case kTapeStopParamCurve:     fCurve = value / 100.0f; break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        const bool stop = fLatch ? fLatched : fHeld;
        if (stop && (fState == State::Running || fState == State::Starting))
            beginStop();
        else if (!stop && (fState == State::Stopping || fState == State::Stopped))
            beginStart();

        const double power = 1.0 + 2.0 * fCurve;
        for (uint32_t i = 0; i < frames; ++i)
        {
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                fLines[ch].write(io[ch][i]);

            // the reel's speed: down to a halt, or back up from where it is
            switch (fState)
            {
            case State::Stopping:
                fElapsed += fStopStep;
                fSpeed = fElapsed >= 1.0 ? 0.0 : fFrom * std::pow(1.0 - fElapsed, power);
                if (fElapsed >= 1.0)
                    fState = State::Stopped;
                break;
            case State::Starting:
                fElapsed += fStartStep;
                fSpeed = fElapsed >= 1.0 ? 1.0 : fFrom + (1.0 - fFrom) * std::pow(fElapsed, power);
                if (fElapsed >= 1.0)
                    fState = State::Landing;
                break;
            default:
                break;
            }
            fDelay = std::clamp(fDelay + 1.0 - fSpeed, 1.0, static_cast<double>(fLines[0].capacity()) - 4.0);

            // the live signal returns over a few ms once the reel is back at full speed
            if (fState == State::Landing)
            {
                fLive = std::min(1.0f, fLive + fEdgeStep);
                if (fLive >= 1.0f)
                    fState = State::Running;
            }
            else if (fState != State::Running)
            {
                fLive = std::max(0.0f, fLive - fEdgeStep);
            }
            const float gain = static_cast<float>(std::min(1.0, fSpeed / kTapeFadeSpeed));
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                if (fLive >= 1.0f)
                    continue;
                const float played = fLines[ch].readFractional(static_cast<float>(fDelay)) * gain;
                io[ch][i] = io[ch][i] * fLive + played * (1.0f - fLive);
            }
        }
    }

    uint32_t latched() const override { return fLatched ? 1u << kTapeStopParamStop : 0u; }

    void setLatched(uint32_t bits) override { fLatched = (bits >> kTapeStopParamStop) & 1u; }

    void reset() override
    {
        for (DelayLine& line : fLines)
            line.reset();
        fState = State::Running;
        fSpeed = 1.0;
        fDelay = 1.0;
        fLive = 1.0f;
    }

private:
    enum class State { Running, Stopping, Stopped, Starting, Landing };

    void beginStop()
    {
        if (fState == State::Running)
            fDelay = 1.0;
        fState = State::Stopping;
        fFrom = fSpeed;
        fElapsed = 0.0;
        fStopStep = 1.0 / (std::max(10.0f, fStopMs) / 1000.0 * fSampleRate);
    }

    void beginStart()
    {
        const double power = 1.0 + 2.0 * fCurve;
        fFrom = fSpeed;
        fElapsed = 0.0;
        if (fStartMs < 1.0f)
        {
            // straight back to the live signal
            fSpeed = 1.0;
            fState = State::Landing;
            return;
        }
        const double samples = fStartMs / 1000.0 * fSampleRate;
        fStartStep = 1.0 / samples;
        // the rising speed averages from + (1 - from) / (power + 1): start that far behind the live signal and the
        // reel catches it up exactly as it reaches full speed
        const double mean = fFrom + (1.0 - fFrom) / (power + 1.0);
        fDelay = std::clamp(1.0 + samples * (1.0 - mean), 1.0, static_cast<double>(fLines[0].capacity()) - 4.0);
        fState = State::Starting;
    }

    double fSampleRate = 48000.0;
    bool fHeld = false, fLatch = false, fLatched = false;
    float fStopMs = 800.0f, fStartMs = 300.0f, fCurve = 0.5f;
    DelayLine fLines[2];
    State fState = State::Running;
    double fSpeed = 1.0, fFrom = 1.0, fElapsed = 0.0, fStopStep = 0.0, fStartStep = 0.0;
    double fDelay = 1.0;
    float fLive = 1.0f, fEdgeStep = 0.004f;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kBounceDirectionNames[] = { "Fall", "Rise" };
constexpr const char* kBounceCountNames[] = { "4", "6", "8", "12", "16", "24" };
constexpr int kBounceCounts[] = { 4, 6, 8, 12, 16, 24 };
constexpr int kMaxBounces = 24;
constexpr double kBounceBufferSeconds = 8.0;

class BounceDelayBlock : public Block {
public:
    BounceDelayBlock() { fMix.snap(0.4f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(kBounceBufferSeconds * sampleRate));
        fUpdate = true;
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kBounceParamTime:      fTimeMs = value; break;
        case kBounceParamSync:      fSync = value >= 0.5f; break;
        case kBounceParamGravity:   fGravity = value / 100.0f; break;
        case kBounceParamDirection: fRise = value >= 0.5f; break;
        case kBounceParamBounces:   fBounces = kBounceCounts[std::clamp(static_cast<int>(value), 0, 5)]; break;
        case kBounceParamBounce:    fBounce = value / 100.0f; break;
        case kBounceParamTone:      fToneHz = value; break;
        case kBounceParamSpread:    fSpread = value / 100.0f; break;
        case kBounceParamMix:       fMix.setTarget(value / 100.0f); return;
        }
        fUpdate = true;
    }

    void setTempo(double bpm) override
    {
        if (fSync && bpm != fBpm)
            fUpdate = true;
        fBpm = bpm;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fUpdate)
            updateTaps();
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mono = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            float out[2] = { 0.0f, 0.0f };
            for (int k = 0; k < fTapCount; ++k)
            {
                Tap& tap = fTaps[k];
                // the delays glide to new settings instead of jumping
                tap.delay += (tap.target - tap.delay) * 0.0005f;
                const float y = tap.tone.lowPass(fLines[0].readFractional(tap.delay)) * tap.gain;
                out[0] += y * tap.left;
                out[1] += y * tap.right;
            }
            fLines[0].write(mono);
            const float mix = fMix.next();
            const float dry = dryGainForMix(mix), wet = wetGainForMix(mix);
            if (numChannels == 1)
            {
                io[0][i] = io[0][i] * dry + 0.5f * (out[0] + out[1]) * wet;
            }
            else
            {
                io[0][i] = io[0][i] * dry + out[0] * wet;
                io[1][i] = io[1][i] * dry + out[1] * wet;
            }
        }
    }

    void reset() override
    {
        for (DelayLine& line : fLines)
            line.reset();
        for (Tap& tap : fTaps)
        {
            tap.tone.reset();
            tap.delay = tap.target;
        }
        fSnap = true;
    }

private:
    struct Tap {
        float delay = 1.0f, target = 1.0f;
        float gain = 0.0f, left = 0.7f, right = 0.7f;
        OnePole tone;
    };

    void updateTaps()
    {
        fUpdate = false;
        const double time = std::max(10.0, syncedMs(kBounceDelayBlockDescriptor.params[kBounceParamTime], fTimeMs,
                                                    fSync, fBpm)) / 1000.0 * fSampleRate;
        // each gap is this much of the last: a light ball (0%) bounces on, a heavy one (100%) settles fast
        const double ratio = 0.93 - 0.45 * fGravity;
        const double limit = static_cast<double>(fLines[0].capacity()) - 8.0;
        double at = 0.0;
        fTapCount = 0;
        for (int k = 0; k < fBounces; ++k)
        {
            // Fall: the first gap is Time, then each shorter; Rise: the same gaps in reverse
            const int n = fRise ? fBounces - 1 - k : k;
            at += time * std::pow(ratio, n);
            if (at > limit)
                break;
            Tap& tap = fTaps[fTapCount];
            const bool first = tap.target <= 1.0f;
            tap.target = static_cast<float>(at);
            if (first)
                tap.delay = tap.target;
            // Fall: loud first, quieter each bounce; Rise: building up to the last
            tap.gain = std::pow(fBounce, static_cast<float>(fRise ? fBounces - 1 - k : k));
            const float pan = (k % 2 == 0 ? -1.0f : 1.0f) * fSpread;
            tap.left = std::sqrt(0.5f * (1.0f - pan));
            tap.right = std::sqrt(0.5f * (1.0f + pan));
            // each bounce a little darker than the last
            const double hz = fToneHz / std::sqrt(1.0 + 0.35 * (fRise ? fBounces - 1 - k : k));
            tap.tone.setLowPass(fSampleRate, std::min(hz, 0.45 * fSampleRate));
            ++fTapCount;
        }
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    float fTimeMs = 400.0f;
    bool fSync = false;
    float fGravity = 0.5f;
    bool fRise = false;
    int fBounces = 12;
    float fBounce = 0.85f, fToneHz = 8000.0f, fSpread = 0.5f;
    bool fUpdate = true, fSnap = true;
    Smoother fMix;
    DelayLine fLines[1];
    std::array<Tap, kMaxBounces> fTaps;
    int fTapCount = 0;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kPatternPitchNames[] = { "Off", "Oct Up", "Oct Down", "Fifth", "Up + Down" };
constexpr float kPatternDefaults[kPatternTaps] = { 0.0f, 0.0f, 70.0f, 0.0f, 0.0f, 50.0f, 0.0f, 35.0f };
constexpr double kPatternMaxStepMs = 1000.0;
constexpr double kPatternWindowMs = 40.0;

class PatternDelayBlock : public Block {
public:
    PatternDelayBlock() { fMix.snap(0.4f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fLine.prepare(static_cast<size_t>(kPatternTaps * kPatternMaxStepMs / 1000.0 * sampleRate) + 16);
        fWindow = static_cast<float>(kPatternWindowMs / 1000.0 * sampleRate);
        for (auto& bus : fBuses)
            for (DelayLine& line : bus.lines)
                line.prepare(static_cast<size_t>(fWindow) + 16);
        fUpdate = true;
        reset();
    }

    void setParam(int index, float value) override
    {
        if (index >= kPatternParamTap1 && index < kPatternParamTap1 + kPatternTaps)
        {
            fLevels[index - kPatternParamTap1] = value / 100.0f;
            return;
        }
        switch (index)
        {
        case kPatternParamTime:     fTimeMs = value; break;
        case kPatternParamSync:     fSync = value >= 0.5f; break;
        case kPatternParamFeedback: fFeedback = std::min(0.9f, value / 100.0f); break;
        case kPatternParamPitch:    fPitch = std::clamp(static_cast<int>(value), 0, 4); break;
        case kPatternParamSpread:   fSpread = value / 100.0f; break;
        case kPatternParamTone:     fToneHz = value; break;
        case kPatternParamMix:      fMix.setTarget(value / 100.0f); return;
        }
        fUpdate = true;
    }

    void setTempo(double bpm) override
    {
        if (fSync && bpm != fBpm)
            fUpdate = true;
        fBpm = bpm;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fUpdate)
            update();
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
            fStep = fStepTarget;
        }
        for (uint32_t i = 0; i < frames; ++i)
        {
            fStep += (fStepTarget - fStep) * 0.0005f;
            const float mono = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            // the taps, into the unshifted sum or one of the two shifting buses, by side
            float plain[2] = { 0.0f, 0.0f };
            float bus[2][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
            for (int k = 0; k < kPatternTaps; ++k)
            {
                if (fLevels[k] <= 0.0f)
                    continue;
                const float y = fLine.readFractional(fStep * static_cast<float>(k + 1)) * fLevels[k];
                const int route = fRoutes[k];
                float* to = route < 0 ? plain : bus[route];
                to[0] += y * fLeft[k];
                to[1] += y * fRight[k];
            }
            float out[2] = { plain[0], plain[1] };
            for (int b = 0; b < 2; ++b)
            {
                if (fRatios[b] == 1.0)
                    continue;
                for (int ch = 0; ch < 2; ++ch)
                {
                    fBuses[b].lines[ch].write(bus[b][ch]);
                    out[ch] += fBuses[b].voices[ch].read(fBuses[b].lines[ch], fRatios[b], fWindow);
                }
            }
            for (int ch = 0; ch < 2; ++ch)
                out[ch] = fTone[ch].lowPass(out[ch]);
            // the pattern again: its last step, fed back in
            const float again = fLine.readFractional(fStep * kPatternTaps) * fFeedback;
            fLine.write(mono + again);
            const float mix = fMix.next();
            const float dry = dryGainForMix(mix), wet = wetGainForMix(mix);
            if (numChannels == 1)
            {
                io[0][i] = io[0][i] * dry + 0.5f * (out[0] + out[1]) * wet;
            }
            else
            {
                io[0][i] = io[0][i] * dry + out[0] * wet;
                io[1][i] = io[1][i] * dry + out[1] * wet;
            }
        }
    }

    void reset() override
    {
        fLine.reset();
        for (auto& bus : fBuses)
            for (int ch = 0; ch < 2; ++ch)
            {
                bus.lines[ch].reset();
                bus.voices[ch].reset(fWindow, 0.5 * ch);
            }
        fTone[0].reset();
        fTone[1].reset();
        fSnap = true;
    }

private:
    struct Bus {
        DelayLine lines[2];
        PitchVoice voices[2];
    };

    void update()
    {
        fUpdate = false;
        const double ms = std::min(kPatternMaxStepMs, syncedMs(kPatternDelayBlockDescriptor.params[kPatternParamTime],
                                                                fTimeMs, fSync, fBpm));
        fStepTarget = static_cast<float>(std::max(1.0, ms / 1000.0 * fSampleRate));
        // Pitch: every other tap (the even steps) goes through bus 0; with Up + Down the odd ones through bus 1
        static constexpr double kShifts[5][2] = { { 0, 0 }, { 12, 0 }, { -12, 0 }, { 7, 0 }, { 12, -12 } };
        for (int b = 0; b < 2; ++b)
            fRatios[b] = semitonesToRatio(kShifts[fPitch][b]);
        for (int k = 0; k < kPatternTaps; ++k)
        {
            const bool even = (k + 1) % 2 == 0;
            fRoutes[k] = fPitch == 0 ? -1 : (even ? 0 : (fPitch == 4 ? 1 : -1));
            const float pan = (k % 2 == 0 ? -1.0f : 1.0f) * fSpread;
            fLeft[k] = std::sqrt(0.5f * (1.0f - pan));
            fRight[k] = std::sqrt(0.5f * (1.0f + pan));
        }
        for (OnePole& tone : fTone)
            tone.setLowPass(fSampleRate, std::min<double>(fToneHz, 0.45 * fSampleRate));
    }

    double fSampleRate = 48000.0;
    double fBpm = 120.0;
    std::array<float, kPatternTaps> fLevels {};
    float fTimeMs = 125.0f;
    bool fSync = false;
    float fFeedback = 0.0f;
    int fPitch = 0;
    float fSpread = 0.6f, fToneHz = 9000.0f;
    bool fUpdate = true, fSnap = true;
    Smoother fMix;
    DelayLine fLine;
    float fStep = 6000.0f, fStepTarget = 6000.0f;
    float fWindow = 1920.0f;
    Bus fBuses[2];
    double fRatios[2] = { 1.0, 1.0 };
    std::array<int, kPatternTaps> fRoutes {};
    std::array<float, kPatternTaps> fLeft {}, fRight {};
    OnePole fTone[2];
};

} // namespace

const BlockDescriptor kTapeStopBlockDescriptor = {
    "tapestop",
    "Tape Stop",
    Category::Delay,
    kTapeStopParamCount,
    {
        ParamSpec { "Stop", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Latch", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Stop Time", "ms", 50.0f, 4000.0f, 800.0f, ParamScale::Log },
        ParamSpec { "Start Time", "ms", 0.0f, 4000.0f, 300.0f, ParamScale::Linear },
        ParamSpec { "Curve", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new TapeStopBlock()); },
};

const BlockDescriptor kBounceDelayBlockDescriptor = {
    "bouncedelay",
    "Bouncing Ball",
    Category::Delay,
    kBounceParamCount,
    {
        ParamSpec { "Time", "ms", 40.0f, 2000.0f, 400.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Gravity", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Direction", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kBounceDirectionNames },
        ParamSpec { "Bounces", "", 0.0f, 5.0f, 3.0f, ParamScale::Choice, kBounceCountNames },
        ParamSpec { "Bounce", "%", 30.0f, 100.0f, 85.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 1000.0f, 16000.0f, 8000.0f, ParamScale::Log },
        ParamSpec { "Spread", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new BounceDelayBlock()); },
    kBounceParamSync,
    kBounceParamTime,
    false,
};

const BlockDescriptor kPatternDelayBlockDescriptor = {
    "patterndelay",
    "Pattern Delay",
    Category::Delay,
    kPatternParamCount,
    {
        ParamSpec { "Tap 1", "%", 0.0f, 100.0f, kPatternDefaults[0], ParamScale::Linear },
        ParamSpec { "Tap 2", "%", 0.0f, 100.0f, kPatternDefaults[1], ParamScale::Linear },
        ParamSpec { "Tap 3", "%", 0.0f, 100.0f, kPatternDefaults[2], ParamScale::Linear },
        ParamSpec { "Tap 4", "%", 0.0f, 100.0f, kPatternDefaults[3], ParamScale::Linear },
        ParamSpec { "Tap 5", "%", 0.0f, 100.0f, kPatternDefaults[4], ParamScale::Linear },
        ParamSpec { "Tap 6", "%", 0.0f, 100.0f, kPatternDefaults[5], ParamScale::Linear },
        ParamSpec { "Tap 7", "%", 0.0f, 100.0f, kPatternDefaults[6], ParamScale::Linear },
        ParamSpec { "Tap 8", "%", 0.0f, 100.0f, kPatternDefaults[7], ParamScale::Linear },
        ParamSpec { "Time", "ms", 20.0f, 1000.0f, 125.0f, ParamScale::Log },
        ParamSpec { "Sync", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Feedback", "%", 0.0f, 90.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Pitch", "", 0.0f, 4.0f, 0.0f, ParamScale::Choice, kPatternPitchNames },
        ParamSpec { "Spread", "%", 0.0f, 100.0f, 60.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 1000.0f, 16000.0f, 9000.0f, ParamScale::Log },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 40.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new PatternDelayBlock()); },
    kPatternParamSync,
    kPatternParamTime,
    false,
};

} // namespace xythos
