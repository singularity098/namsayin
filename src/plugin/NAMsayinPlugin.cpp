// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoPlugin.hpp"

#include "engine/Engine.hpp"

#include <cstdio>
#include <cstring>
#include <optional>

START_NAMESPACE_DISTRHO

using namespace namsayin;

// Plugin state keys
static constexpr const char* kStateChain = "chain";

enum StateIndex : uint32_t {
    kStateIndexChain,
    kStateCount
};

class NAMsayinPlugin : public Plugin
{
public:
    NAMsayinPlugin()
        : Plugin(kParamCount, 0, kStateCount)
    {
        fEngine.prepare(getSampleRate(), getBufferSize());
    }

protected:
    const char* getLabel() const override { return "NAMsayin"; }
    const char* getDescription() const override { return "Neural Amp Modeler guitar rig"; }
    const char* getMaker() const override { return "singularity098"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index < kGlobalParamCount)
        {
            const GlobalParamSpec& spec = globalParamSpec(index);
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.unit = spec.unit;
            parameter.ranges.min = spec.min;
            parameter.ranges.max = spec.max;
            parameter.ranges.def = spec.def;
            parameter.hints = (spec.flags & kParamFlagOutput) ? kParameterIsOutput : kParameterIsAutomatable;
            if (spec.flags & kParamFlagToggle)
                parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            if (index == kParamBypass)
                parameter.designation = kParameterDesignationBypass;
            return;
        }

        // Slot parameters: generic names, because the block in a slot changes (docs/Architecture.md 4.3)
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount);
        char name[32];
        char symbol[32];
        if (param == 0)
        {
            std::snprintf(name, sizeof(name), "Slot %02d Bypass", slot + 1);
            std::snprintf(symbol, sizeof(symbol), "s%02d_bypass", slot + 1);
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
        }
        else
        {
            std::snprintf(name, sizeof(name), "Slot %02d P%d", slot + 1, param);
            std::snprintf(symbol, sizeof(symbol), "s%02d_p%d", slot + 1, param);
            parameter.hints = kParameterIsAutomatable;
        }
        parameter.name = name;
        parameter.symbol = symbol;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1.0f;
        parameter.ranges.def = 0.0f;
    }

    void initState(const uint32_t index, State& state) override
    {
        switch (index)
        {
        case kStateIndexChain:
            state.key = kStateChain;
            state.defaultValue = "";
            state.hints = kStateIsOnlyForDSP;
            break;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return fEngine.getParameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        fEngine.setParameter(index, value);
    }

    void setState(const char* const key, const char* const value) override
    {
        if (std::strcmp(key, kStateChain) != 0)
            return;

        std::string error;
        const std::optional<ChainState> chain = ChainState::fromJson(value, &error);
        if (!chain || !fEngine.setChain(*chain, &error))
            d_stderr2("NAMsayin: ignoring invalid chain state: %s", error.c_str());
    }

    void activate() override
    {
        fEngine.prepare(getSampleRate(), getBufferSize());
    }

    void run(const float** const inputs, float** const outputs, const uint32_t frames) override
    {
        fEngine.process(inputs, outputs, frames);

        const uint32_t latency = fEngine.latency();
        if (latency != fReportedLatency)
        {
            fReportedLatency = latency;
            setLatency(latency);
        }
    }

private:
    Engine fEngine;
    uint32_t fReportedLatency = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMsayinPlugin)
};

Plugin* createPlugin()
{
    return new NAMsayinPlugin();
}

END_NAMESPACE_DISTRHO
