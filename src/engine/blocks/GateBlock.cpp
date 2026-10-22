// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/GateBlock.hpp"


#include <algorithm>
#include <cmath>

namespace namsayin {

namespace {

constexpr const char* kTypeNames[] = { "Gate", "Expander" };
constexpr const char* kKeyNames[] = { "Block In", "Rig In" };

constexpr float kDbPerLog2 = 6.0205999f; // 20 * log10(2)
constexpr float kExpanderRatio = 3.0f;   // dB of attenuation per dB below the threshold, minus one

class GateBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        // detector: a peak follower with a fast attack and a release long enough to ride over a low E's cycle
        fDetectAttack = coef(0.3f);
        fDetectRelease = coef(30.0f);
        updateTimes();
        reset();
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kGateParamThreshold:
            fThresholdDb = value;
            break;
        case kGateParamReduction:
            fReductionDb = value;
            break;
        case kGateParamAttack:
            fAttackMs = value;
            updateTimes();
            break;
        case kGateParamHold:
            fHoldMs = value;
            updateTimes();
            break;
        case kGateParamRelease:
            fReleaseMs = value;
            updateTimes();
            break;
        case kGateParamHysteresis:
            fHysteresisDb = value;
            break;
        case kGateParamType:
            fType = static_cast<GateType>(std::clamp(static_cast<int>(value), 0, 1));
            break;
        case kGateParamKey:
            fKey = static_cast<GateKey>(std::clamp(static_cast<int>(value), 0, 1));
            break;
        }
    }

    void setRigInput(const float* const* input, uint32_t numChannels) override
    {
        fRig[0] = input[0];
        fRig[1] = numChannels > 1 ? input[1] : nullptr;
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        const bool rig = fKey == GateKey::Rig && fRig[0] != nullptr;
        const float* key0 = rig ? fRig[0] : io[0];
        const float* key1 = rig ? fRig[1] : (numChannels > 1 ? io[1] : nullptr);

        float maxReduction = 0.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            float peak = std::fabs(key0[i]);
            if (key1)
                peak = std::max(peak, std::fabs(key1[i]));
            fLevel = peak + (fLevel - peak) * (peak > fLevel ? fDetectAttack : fDetectRelease);
            const float levelDb = fLevel > 1e-7f ? kDbPerLog2 * std::log2(fLevel) : -140.0f;

            // open above the threshold; close only once below threshold - hysteresis for the hold time
            if (levelDb >= fThresholdDb)
            {
                fOpen = true;
                fHoldLeft = fHoldSamples;
            }
            else if (fOpen && levelDb < fThresholdDb - fHysteresisDb)
            {
                if (fHoldLeft > 0)
                    --fHoldLeft;
                else
                    fOpen = false;
            }

            float target = 0.0f;
            if (fType == GateType::Gate)
                target = fOpen ? 0.0f : -fReductionDb;
            else if (!fOpen)
                target = std::max(-fReductionDb, (levelDb - (fThresholdDb - fHysteresisDb)) * (kExpanderRatio - 1.0f));

            // opening: attack; closing: release
            fGainDb = target + (fGainDb - target) * (target > fGainDb ? fAttackCoef : fReleaseCoef);
            maxReduction = std::max(maxReduction, -fGainDb);

            const float gain = std::exp2(fGainDb / kDbPerLog2);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= gain;
        }
        fMeter = maxReduction;
        fRig[0] = fRig[1] = nullptr;
    }

    float meter() const override { return fMeter; }

    void reset() override
    {
        fLevel = 0.0f;
        fOpen = false;
        fHoldLeft = 0;
        fGainDb = -fReductionDb;
        fMeter = 0.0f;
    }

private:
    float coef(float ms) const
    {
        return static_cast<float>(std::exp(-1.0 / (std::max(0.01f, ms) / 1000.0 * fSampleRate)));
    }

    void updateTimes()
    {
        fAttackCoef = coef(fAttackMs);
        fReleaseCoef = coef(fReleaseMs);
        fHoldSamples = static_cast<int>(fHoldMs / 1000.0 * fSampleRate);
    }

    double fSampleRate = 48000.0;
    float fThresholdDb = -55.0f;
    float fReductionDb = 80.0f;
    float fAttackMs = 1.0f;
    float fHoldMs = 50.0f;
    float fReleaseMs = 150.0f;
    float fHysteresisDb = 4.0f;
    GateType fType = GateType::Gate;
    GateKey fKey = GateKey::Block;
    const float* fRig[2] = { nullptr, nullptr };

    float fDetectAttack = 0.0f, fDetectRelease = 0.0f;
    float fAttackCoef = 0.0f, fReleaseCoef = 0.0f;
    int fHoldSamples = 0;
    float fLevel = 0.0f;
    bool fOpen = false;
    int fHoldLeft = 0;
    float fGainDb = 0.0f;
    float fMeter = 0.0f;
};

} // namespace

const BlockDescriptor kGateBlockDescriptor = {
    "gate",
    "Noise Gate",
    Category::Dynamics,
    kGateParamCount,
    {
        ParamSpec { "Threshold", "dB", -90.0f, 0.0f, -55.0f, ParamScale::Linear },
        ParamSpec { "Reduction", "dB", 0.0f, 90.0f, 80.0f, ParamScale::Linear },
        ParamSpec { "Attack", "ms", 0.1f, 50.0f, 1.0f, ParamScale::Log },
        ParamSpec { "Hold", "ms", 0.0f, 500.0f, 50.0f, ParamScale::Linear },
        ParamSpec { "Release", "ms", 5.0f, 2000.0f, 150.0f, ParamScale::Log },
        ParamSpec { "Hysteresis", "dB", 0.0f, 12.0f, 4.0f, ParamScale::Linear },
        ParamSpec { "Type", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kTypeNames },
        ParamSpec { "Key", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kKeyNames },
    },
    [] { return std::unique_ptr<Block>(new GateBlock()); },
    -1,
    -1,
    false,
    "Reduction",
    60.0f,
};

} // namespace namsayin
