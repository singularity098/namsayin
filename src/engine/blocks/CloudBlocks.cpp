// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/CloudBlocks.hpp"

#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Lfo.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/Svf.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace namsayin {

namespace {

// xorshift32, for the grains' and voices' randomness (deterministic per block)
class Random {
public:
    explicit Random(uint32_t seed) : fState(seed ? seed : 1u) {}

    // 0..1
    float next()
    {
        fState ^= fState << 13;
        fState ^= fState >> 17;
        fState ^= fState << 5;
        return static_cast<float>(fState >> 8) / 16777216.0f;
    }

private:
    uint32_t fState;
};

constexpr const char* kGrainPitchNames[] = { "0", "+12", "-12", "+/-12", "+7", "Oct + 5th", "Random" };
constexpr double kGrainBufferSeconds = 2.5;
constexpr int kMaxGrains = 32;

class GrainCloudBlock : public Block {
public:
    GrainCloudBlock() { fMix.snap(0.5f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        for (DelayLine& line : fLines)
            line.prepare(static_cast<size_t>(kGrainBufferSeconds * sampleRate));
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kGrainParamSize:     fSizeMs = value; break;
        case kGrainParamDensity:  fDensity = value; break;
        case kGrainParamDelay:    fDelayMs = value; break;
        case kGrainParamSpray:    fSprayMs = value; break;
        case kGrainParamPitch:    fPitch = std::clamp(static_cast<int>(value), 0, 6); break;
        case kGrainParamReverse:  fReverse = value / 100.0f; break;
        case kGrainParamFeedback: fFeedback = std::min(0.9f, value / 100.0f); break;
        case kGrainParamWidth:    fWidth = value / 100.0f; break;
        case kGrainParamFreeze:   fFreeze = value >= 0.5f; break;
        case kGrainParamMix:      fMix.setTarget(value / 100.0f); break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
        }
        // grains overlap about density x size at once; keep the cloud near the input's level
        const float overlap = std::max(1.0f, fDensity * fSizeMs / 1000.0f);
        const float norm = 1.0f / std::sqrt(overlap);
        const float advance = fFreeze ? 0.0f : 1.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (--fUntilNext <= 0)
            {
                spawn();
                // the next grain: on average 1/density away, give or take half of that
                const double interval = fSampleRate / std::max(0.5f, fDensity);
                fUntilNext = static_cast<int>(interval * (0.5 + fRandom.next()));
            }
            float out[2] = { 0.0f, 0.0f };
            for (Grain& grain : fGrains)
            {
                if (grain.left <= 0)
                    continue;
                const float p = static_cast<float>(grain.age) / static_cast<float>(grain.length);
                const float s = std::sin(static_cast<float>(M_PI) * p);
                const float w = s * s * norm;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const DelayLine& line = fLines[numChannels > 1 ? ch : 0];
                    out[ch] += line.readFractional(std::max(1.0f, grain.delay)) * w * grain.gain[ch];
                }
                grain.delay += advance - grain.step;
                ++grain.age;
                --grain.left;
            }
            const float mix = fMix.next();
            const float dry = dryGainForMix(mix), wet = wetGainForMix(mix);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
            {
                const float x = io[ch][i];
                const float cloud = numChannels > 1 ? out[ch] : 0.5f * (out[0] + out[1]);
                if (!fFreeze)
                    fLines[ch].write(x + std::tanh(cloud * fFeedback));
                io[ch][i] = x * dry + cloud * wet;
            }
        }
    }

    void reset() override
    {
        for (DelayLine& line : fLines)
            line.reset();
        for (Grain& grain : fGrains)
            grain.left = 0;
        fUntilNext = 0;
        fSnap = true;
    }

private:
    struct Grain {
        float delay = 1.0f; // samples behind the write head
        float step = 1.0f;  // how far the read moves per sample (negative: backwards)
        int length = 1, age = 0, left = 0;
        float gain[2] = { 0.7f, 0.7f };
    };

    double pitchRatio()
    {
        static constexpr int kRandom[] = { -12, -5, 0, 7, 12 };
        int semitones = 0;
        const float r = fRandom.next();
        switch (fPitch)
        {
        case 1: semitones = 12; break;
        case 2: semitones = -12; break;
        case 3: semitones = r < 0.5f ? 12 : -12; break;
        case 4: semitones = 7; break;
        case 5: semitones = r < 0.34f ? 0 : (r < 0.67f ? 7 : 12); break;
        case 6: semitones = kRandom[std::min(4, static_cast<int>(r * 5.0f))]; break;
        default: break;
        }
        return std::exp2(semitones / 12.0);
    }

    void spawn()
    {
        Grain* grain = nullptr;
        for (Grain& g : fGrains)
            if (g.left <= 0)
            {
                grain = &g;
                break;
            }
        if (grain == nullptr)
            return;
        const auto length = std::max(16, static_cast<int>(fSizeMs / 1000.0f * fSampleRate));
        const bool reverse = fRandom.next() < fReverse;
        const auto ratio = static_cast<float>(pitchRatio());
        grain->step = reverse ? -ratio : ratio;
        grain->length = grain->left = length;
        grain->age = 0;
        // far enough back that a faster read never catches the write head (frozen: nothing new is written)
        const float advance = fFreeze ? 0.0f : 1.0f;
        const float travel = (grain->step - advance) * static_cast<float>(length);
        const float sprayed = (fDelayMs + fSprayMs * fRandom.next()) / 1000.0f * static_cast<float>(fSampleRate);
        const float capacity = static_cast<float>(fLines[0].capacity()) - 4.0f;
        float start = sprayed + std::max(0.0f, travel) + 2.0f;
        // backwards grains read further back as they go: keep them inside the buffer
        start = std::clamp(start, 2.0f, capacity - std::max(0.0f, -travel));
        grain->delay = std::max(2.0f, start);
        const float pan = (fRandom.next() * 2.0f - 1.0f) * fWidth;
        grain->gain[0] = std::sqrt(0.5f * (1.0f - pan));
        grain->gain[1] = std::sqrt(0.5f * (1.0f + pan));
    }

    double fSampleRate = 48000.0;
    float fSizeMs = 80.0f, fDensity = 15.0f, fDelayMs = 50.0f, fSprayMs = 300.0f;
    int fPitch = 0;
    float fReverse = 0.2f, fFeedback = 0.0f, fWidth = 0.8f;
    bool fFreeze = false;
    Smoother fMix;
    DelayLine fLines[2];
    std::array<Grain, kMaxGrains> fGrains;
    int fUntilNext = 0;
    Random fRandom { 0x9e3779b9u };
    bool fSnap = true;
};

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* kSwarmVoiceNames[] = { "4", "8", "12", "16" };
constexpr int kMaxSwarm = 16;
constexpr double kMaxSpreadMs = 40.0;

class SwarmBlock : public Block {
public:
    SwarmBlock() { fMix.snap(0.5f); }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        fMix.prepare(sampleRate);
        fLine.prepare(static_cast<size_t>(2.0 * kMaxSpreadMs / 1000.0 * sampleRate) + 8);
        Random random(0x5eed1234u);
        for (int v = 0; v < kMaxSwarm; ++v)
        {
            Voice& voice = fVoices[v];
            voice.place = (static_cast<float>(v) + 0.3f + 0.4f * random.next()) / kMaxSwarm;
            voice.wingHz = 25.0f + 85.0f * random.next();
            voice.wingPhase = random.next();
            voice.pan = (v % 2 == 0 ? -1.0f : 1.0f) * (0.3f + 0.7f * random.next());
            voice.drift.prepare(sampleRate, fSpeed, 0x1000u + 977u * static_cast<uint32_t>(v));
        }
        updateVoices();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kSwarmParamVoices: fCount = 4 * (std::clamp(static_cast<int>(value), 0, 3) + 1); break;
        case kSwarmParamSpread: fSpreadMs = value; break;
        case kSwarmParamDrift:  fDriftCents = value; break;
        case kSwarmParamSpeed:  fSpeed = value; break;
        case kSwarmParamBuzz:   fBuzz = value / 100.0f; break;
        case kSwarmParamTone:   fToneHz = value; break;
        case kSwarmParamWidth:  fWidth = value / 100.0f; break;
        case kSwarmParamMix:    fMix.setTarget(value / 100.0f); return;
        }
        fUpdate = true;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (fUpdate)
            updateVoices();
        if (fSnap)
        {
            fSnap = false;
            fMix.snap(fMix.target());
        }
        const float norm = 1.0f / std::sqrt(static_cast<float>(fCount));
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mono = numChannels > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
            fLine.write(mono);
            float left = 0.0f, right = 0.0f;
            for (int v = 0; v < fCount; ++v)
            {
                Voice& voice = fVoices[v];
                const float drift = voice.drift.next();
                const float delay = std::max(2.0f, voice.base + voice.depth * drift);
                // the wings: a flutter in level, its rate wandering with the drift
                voice.wingPhase += voice.wingStep * (1.0f + 0.15f * drift);
                voice.wingPhase -= std::floor(voice.wingPhase);
                const float wing = 1.0f - fBuzz * 0.5f * (1.0f + std::sin(6.2831853f * voice.wingPhase));
                const float y = fLine.readFractional(delay) * wing;
                left += y * voice.left;
                right += y * voice.right;
            }
            const float mix = fMix.next();
            const float dry = dryGainForMix(mix), wet = wetGainForMix(mix) * norm;
            left = fTone[0].lowPass(left) * wet;
            right = fTone[1].lowPass(right) * wet;
            if (numChannels == 1)
            {
                io[0][i] = io[0][i] * dry + 0.5f * (left + right);
            }
            else
            {
                io[0][i] = io[0][i] * dry + left;
                io[1][i] = io[1][i] * dry + right;
            }
        }
    }

    void reset() override
    {
        fLine.reset();
        fTone[0].reset();
        fTone[1].reset();
        fSnap = true;
    }

private:
    struct Voice {
        float place = 0.5f; // where in the spread its delay sits
        float base = 100.0f, depth = 0.0f;
        float wingHz = 50.0f, wingPhase = 0.0f, wingStep = 0.0f;
        float pan = 0.0f, left = 0.7f, right = 0.7f;
        RandomLfo drift;
    };

    void updateVoices()
    {
        fUpdate = false;
        const double spread = std::min(kMaxSpreadMs, static_cast<double>(fSpreadMs)) / 1000.0 * fSampleRate;
        // a delay swinging by A samples at f Hz bends the pitch by about 2 pi f A / fs: Drift sets A for Speed
        const double bend = std::exp2(fDriftCents / 1200.0) - 1.0;
        const double depth = bend * fSampleRate / (2.0 * M_PI * std::max(0.05f, fSpeed));
        for (int v = 0; v < kMaxSwarm; ++v)
        {
            Voice& voice = fVoices[v];
            voice.base = static_cast<float>(3.0 + spread * voice.place);
            voice.depth = static_cast<float>(std::min(depth, static_cast<double>(voice.base) - 2.0));
            voice.wingStep = static_cast<float>(voice.wingHz / fSampleRate);
            const float pan = voice.pan * fWidth;
            voice.left = std::sqrt(0.5f * (1.0f - pan));
            voice.right = std::sqrt(0.5f * (1.0f + pan));
            // each voice drifts at its own rate, around Speed
            voice.drift.setRate(fSampleRate, fSpeed * (0.6 + 0.8 * voice.place));
        }
        for (OnePole& tone : fTone)
            tone.setLowPass(fSampleRate, std::min<double>(fToneHz, 0.45 * fSampleRate));
    }

    double fSampleRate = 48000.0;
    int fCount = 12;
    float fSpreadMs = 15.0f, fDriftCents = 15.0f, fSpeed = 2.0f, fBuzz = 0.3f, fToneHz = 6000.0f, fWidth = 0.8f;
    bool fUpdate = true;
    Smoother fMix;
    DelayLine fLine;
    std::array<Voice, kMaxSwarm> fVoices;
    OnePole fTone[2];
    bool fSnap = true;
};

} // namespace

const BlockDescriptor kGrainCloudBlockDescriptor = {
    "graincloud",
    "Grain Cloud",
    Category::Delay,
    kGrainParamCount,
    {
        ParamSpec { "Size", "ms", 20.0f, 300.0f, 80.0f, ParamScale::Log },
        ParamSpec { "Density", "/s", 1.0f, 60.0f, 15.0f, ParamScale::Log },
        ParamSpec { "Delay", "ms", 0.0f, 1000.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Spray", "ms", 0.0f, 1500.0f, 300.0f, ParamScale::Linear },
        ParamSpec { "Pitch", "", 0.0f, 6.0f, 0.0f, ParamScale::Choice, kGrainPitchNames },
        ParamSpec { "Reverse", "%", 0.0f, 100.0f, 20.0f, ParamScale::Linear },
        ParamSpec { "Feedback", "%", 0.0f, 90.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 80.0f, ParamScale::Linear },
        ParamSpec { "Freeze", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new GrainCloudBlock()); },
};

const BlockDescriptor kSwarmBlockDescriptor = {
    "swarm",
    "Swarm",
    Category::Modulation,
    kSwarmParamCount,
    {
        ParamSpec { "Voices", "", 0.0f, 3.0f, 2.0f, ParamScale::Choice, kSwarmVoiceNames },
        ParamSpec { "Spread", "ms", 2.0f, 40.0f, 15.0f, ParamScale::Log },
        ParamSpec { "Drift", "ct", 0.0f, 60.0f, 15.0f, ParamScale::Linear },
        ParamSpec { "Speed", "Hz", 0.1f, 10.0f, 2.0f, ParamScale::Log },
        ParamSpec { "Buzz", "%", 0.0f, 100.0f, 30.0f, ParamScale::Linear },
        ParamSpec { "Tone", "Hz", 1000.0f, 16000.0f, 6000.0f, ParamScale::Log },
        ParamSpec { "Width", "%", 0.0f, 100.0f, 80.0f, ParamScale::Linear },
        ParamSpec { "Mix", "%", 0.0f, 100.0f, 50.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new SwarmBlock()); },
};

} // namespace namsayin
