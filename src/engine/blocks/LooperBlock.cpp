// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/LooperBlock.hpp"

#include "engine/dsp/Smoother.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace namsayin {

namespace {

constexpr uint32_t kFadeSamples = 256; // the loop's ends are faded this long when it's closed, so the seam doesn't click

class LooperBlock : public Block {
public:
    LooperBlock()
    {
        fLevel.snap(1.0f);
        fFeedback.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        // the loop holds a fixed number of samples (no reallocation if prepared again at another rate)
        for (std::vector<float>& buffer : fBuffer)
            if (buffer.size() != kLooperMaxSamples)
                buffer.assign(kLooperMaxSamples, 0.0f);
        fLevel.prepare(sampleRate);
        fFeedback.prepare(sampleRate);
        fFade.prepare(sampleRate, 0.005);
        clear();
    }

    void setParam(int index, float value) override
    {
        const bool on = value >= 0.5f;
        switch (index)
        {
        case kLooperParamRecDub:
            if (on && !fRecDubHeld)
                recDub();
            fRecDubHeld = on;
            break;
        case kLooperParamPlayStop:
            if (on && !fPlayStopHeld)
                playStop();
            fPlayStopHeld = on;
            break;
        case kLooperParamClear:
            if (on && !fClearHeld)
                clear();
            fClearHeld = on;
            break;
        case kLooperParamLevel:    fLevel.setTarget(value / 100.0f); break;
        case kLooperParamFeedback: fFeedback.setTarget(value / 100.0f); break;
        case kLooperParamReverse:  fReverse = on; break;
        case kLooperParamHalf:     fHalf = on; break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float level = fLevel.next();
            const float feedback = fFeedback.next();
            // playback fades in and out on start and stop
            fFade.setTarget(fState == LooperState::Playing || fState == LooperState::Overdubbing ? 1.0f : 0.0f);
            const float fade = fFade.next();

            if (fState == LooperState::Recording)
            {
                for (uint32_t ch = 0; ch < 2; ++ch)
                    fBuffer[ch][fLength] = io[numChannels > 1 ? ch : 0][i];
                if (++fLength >= kLooperMaxSamples)
                    closeLoop();
                continue;
            }
            if (fLength == 0 || (fState == LooperState::Stopped && fade <= 0.0f))
                continue;

            const auto index = static_cast<uint32_t>(fPosition);
            const double frac = fPosition - index;
            const uint32_t next = index + 1 < fLength ? index + 1 : 0;
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                std::vector<float>& buffer = fBuffer[ch];
                const float played = static_cast<float>(buffer[index] + (buffer[next] - buffer[index]) * frac);
                if (fState == LooperState::Overdubbing)
                    buffer[index] = buffer[index] * feedback + io[ch][i];
                io[ch][i] += played * level * fade;
            }
            if (fState == LooperState::Overdubbing && numChannels == 1)
                fBuffer[1][index] = fBuffer[0][index];

            if (fState != LooperState::Stopped)
            {
                const double step = fHalf ? 0.5 : 1.0;
                fPosition += fReverse ? -step : step;
                if (fPosition >= fLength)
                    fPosition -= fLength;
                if (fPosition < 0.0)
                    fPosition += fLength;
            }
        }

        LooperStatus status;
        status.state = fState;
        status.lengthTenths = static_cast<int>(fLength / fSampleRate * 10.0);
        status.progress = fLength > 0 && fState != LooperState::Recording
                              ? std::min(99, static_cast<int>(fPosition / fLength * 100.0))
                              : 0;
        fMeter = status.encode();
    }

    float meter() const override { return fMeter; }

    // The loop survives chain changes (the engine reuses the block); reset() only rewinds
    void reset() override { fPosition = 0.0; }

private:
    void recDub()
    {
        switch (fState)
        {
        case LooperState::Empty:
            fLength = 0;
            fState = LooperState::Recording;
            break;
        case LooperState::Recording:
            closeLoop();
            break;
        case LooperState::Playing:
            fState = LooperState::Overdubbing;
            break;
        case LooperState::Overdubbing:
            fState = LooperState::Playing;
            break;
        case LooperState::Stopped:
            fPosition = fReverse ? fLength - 1 : 0.0;
            fState = LooperState::Overdubbing;
            break;
        }
    }

    void playStop()
    {
        switch (fState)
        {
        case LooperState::Recording:
            closeLoop();
            break;
        case LooperState::Playing:
        case LooperState::Overdubbing:
            fState = LooperState::Stopped;
            break;
        case LooperState::Stopped:
            fPosition = fReverse ? fLength - 1 : 0.0;
            fState = LooperState::Playing;
            break;
        case LooperState::Empty:
            break;
        }
    }

    void closeLoop()
    {
        if (fLength < kFadeSamples * 4)
        {
            // too short to be a loop: treat as a mistake
            clear();
            return;
        }
        // fade the ends so the seam doesn't click
        for (std::vector<float>& buffer : fBuffer)
        {
            for (uint32_t k = 0; k < kFadeSamples; ++k)
            {
                const float g = static_cast<float>(k) / kFadeSamples;
                buffer[k] *= g;
                buffer[fLength - 1 - k] *= g;
            }
        }
        fPosition = 0.0;
        fState = LooperState::Playing;
        fFade.snap(1.0f);
    }

    void clear()
    {
        fState = LooperState::Empty;
        fLength = 0;
        fPosition = 0.0;
        fFade.snap(0.0f);
    }

    double fSampleRate = 48000.0;
    std::vector<float> fBuffer[2];
    LooperState fState = LooperState::Empty;
    uint32_t fLength = 0;
    double fPosition = 0.0;
    bool fReverse = false, fHalf = false;
    bool fRecDubHeld = false, fPlayStopHeld = false, fClearHeld = false;
    Smoother fLevel, fFeedback, fFade;
    float fMeter = 0.0f;
};

} // namespace

const BlockDescriptor kLooperBlockDescriptor = {
    "looper",
    "Looper",
    Category::Delay,
    kLooperParamCount,
    {
        ParamSpec { "Rec/Dub", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Play/Stop", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Clear", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Level", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Feedback", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Reverse", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Half Speed", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
    },
    [] { return std::unique_ptr<Block>(new LooperBlock()); },
    -1,
    -1,
    false,
    "Loop",
    0.0f,
    MeterStyle::Looper,
};

} // namespace namsayin
