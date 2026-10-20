// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"
#include "engine/BlockRegistry.hpp"

#include <atomic>
#include <cmath>
#include <vector>

namespace testing {

extern thread_local bool gCountAllocations;
extern std::atomic<int> gAllocations;

// Counts heap allocations on this thread while alive
class AllocationGuard {
public:
    AllocationGuard()
    {
        gAllocations = 0;
        gCountAllocations = true;
    }
    ~AllocationGuard() { gCountAllocations = false; }
    int count() const { return gAllocations.load(); }
};

// Test block: a gain (param 0, -24..24 dB) with optional latency and a configure() failure, set via config:
// {"latency": samples, "fail": true}
class GainBlock : public namsayin::Block {
public:
    static inline int sCreated = 0;

    GainBlock() { ++sCreated; }

    void prepare(double, uint32_t) override {}

    bool configure(const nlohmann::json& config) override
    {
        fLatency = config.value("latency", 0u);
        fDelay.assign(fLatency + 1, 0.0f);
        return !config.value("fail", false);
    }

    void setParam(int index, float value) override
    {
        if (index == 0)
            fGain = std::pow(10.0f, value / 20.0f);
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        for (uint32_t ch = 0; ch < numChannels; ++ch)
            for (uint32_t i = 0; i < frames; ++i)
                io[ch][i] *= fGain;

        // Delay channel 0 by fLatency samples
        if (fLatency > 0)
        {
            for (uint32_t i = 0; i < frames; ++i)
            {
                fDelay[fPos] = io[0][i];
                fPos = (fPos + 1) % fDelay.size();
                io[0][i] = fDelay[fPos];
            }
        }
    }

    uint32_t latency() const override { return fLatency; }

private:
    float fGain = 1.0f;
    uint32_t fLatency = 0;
    std::vector<float> fDelay;
    size_t fPos = 0;
};

inline const namsayin::BlockDescriptor kGainDescriptor = {
    "test.gain",
    "Test Gain",
    namsayin::Category::Dynamics,
    1,
    { namsayin::ParamSpec { "Gain", "dB", -24.0f, 24.0f, 0.0f, namsayin::ParamScale::Linear } },
    [] { return std::unique_ptr<namsayin::Block>(new GainBlock()); },
};

inline const namsayin::BlockRegistry& testRegistry()
{
    static const namsayin::BlockRegistry registry({ &kGainDescriptor });
    return registry;
}

} // namespace testing
