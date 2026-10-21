// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"
#include "engine/BlockRegistry.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string>
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
// {"latency": samples, "fail": true}. Its meter() reports the host tempo it was given.
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
    void setTempo(double bpm) override { fTempo = bpm; }
    float meter() const override { return static_cast<float>(fTempo); }

private:
    double fTempo = 0.0;
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

// Processes `signal` (one vector per channel, 1 or 2, equal lengths) through a block in place, in buffers of
// `bufferSize`. Adds the heap allocations made inside process() to `allocations`.
inline void runBlock(namsayin::Block& block, std::vector<std::vector<float>>& signal, uint32_t bufferSize = 128,
                     int* allocations = nullptr)
{
    const auto numChannels = static_cast<uint32_t>(signal.size());
    const size_t length = signal[0].size();
    for (size_t t = 0; t < length; t += bufferSize)
    {
        const auto n = static_cast<uint32_t>(std::min<size_t>(bufferSize, length - t));
        float* io[2] = { signal[0].data() + t, signal[numChannels > 1 ? 1 : 0].data() + t };
        AllocationGuard guard;
        block.process(io, numChannels, n);
        if (allocations)
            *allocations += guard.count();
    }
}

// Sets every parameter of a freshly created block to its default
inline std::unique_ptr<namsayin::Block> createWithDefaults(const namsayin::BlockDescriptor& type, double rate,
                                                           uint32_t maxFrames = 128)
{
    std::unique_ptr<namsayin::Block> block = type.create();
    block->prepare(rate, maxFrames);
    block->configure(nlohmann::json::object());
    for (int p = 0; p < type.numParams; ++p)
        block->setParam(p, type.params[p].def);
    return block;
}

inline double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

inline void setEnv(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

inline const namsayin::BlockRegistry& testRegistry()
{
    static const namsayin::BlockRegistry registry({ &kGainDescriptor });
    return registry;
}

} // namespace testing
