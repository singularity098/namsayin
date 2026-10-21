// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoPlugin.hpp"

#include "engine/Engine.hpp"
#include "state/ChainExchange.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>

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

        std::random_device random;
        fInstanceId = 1 + random() % kMaxExchangeValue;
        fEngine.setParameter(kParamInstanceId, static_cast<float>(fInstanceId));
        fExchangeThread = std::thread([this] { watchExchange(); });
    }

    ~NAMsayinPlugin() override
    {
        fStopExchange = true;
        if (fExchangeThread.joinable())
            fExchangeThread.join();
        std::remove(chainExchangePath(fInstanceId).c_str());
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
        if (!isSlotParam(index))
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
            if (spec.flags & kParamFlagInteger)
                parameter.hints |= kParameterIsInteger;
            if (spec.flags & kParamFlagHidden)
                parameter.hints = (parameter.hints & ~kParameterIsAutomatable) | kParameterIsHidden;
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
            // Not DSP-only: the UI edits the chain and needs it back when it opens
            state.hints = 0;
            break;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return fEngine.getParameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index == kParamInstanceId || index == kParamMidiLastCc || index == kParamMidiCcCount
            || index == kParamMidiLastValue)
            return; // outputs, owned by the plugin
        fEngine.setParameter(index, value);
    }

    void setState(const char* const key, const char* const value) override
    {
        if (std::strcmp(key, kStateChain) == 0)
            applyChain(value);
    }

    String getState(const char* const key) const override
    {
        if (std::strcmp(key, kStateChain) != 0)
            return String();
        const std::lock_guard<std::mutex> lock(fChainMutex);
        return String(fChainJson.c_str());
    }

    void activate() override
    {
        fEngine.prepare(getSampleRate(), getBufferSize());
    }

    void run(const float** const inputs, float** const outputs, const uint32_t frames,
             const MidiEvent* const midiEvents, const uint32_t midiEventCount) override
    {
        // MIDI CCs (learn and mapped controls), applied at the start of the buffer
        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const MidiEvent& event = midiEvents[i];
            if (event.size != 3 || (event.data[0] & 0xf0) != 0xb0)
                continue;
            const Engine::CcResult result = fEngine.midiCc(event.data[0] & 0x0f, event.data[1], event.data[2]);
            if (result.param >= 0)
                requestParameterValueChange(static_cast<uint32_t>(result.param), result.value);
        }

        const TimePosition& time = getTimePosition();
        if (time.bbt.valid)
            fEngine.setTempo(time.bbt.beatsPerMinute);
        fEngine.process(inputs, outputs, frames);

        const uint32_t latency = fEngine.latency();
        if (latency != fReportedLatency)
        {
            fReportedLatency = latency;
            setLatency(latency);
        }
    }

private:
    // Non-RT. Builds the chain and keeps its JSON for getState() and the exchange file.
    void applyChain(const char* const json)
    {
        {
            // the same chain often arrives twice (state message and exchange file)
            const std::lock_guard<std::mutex> lock(fChainMutex);
            if (fChainJson == json)
                return;
        }
        std::string error;
        const std::optional<ChainState> chain = ChainState::fromJson(json, &error);
        if (!chain || !fEngine.setChain(*chain, &error))
        {
            d_stderr2("NAMsayin: ignoring invalid chain state: %s", error.c_str());
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(fChainMutex);
            fChainJson = json;
        }
        writeChainExchange(fInstanceId, json);
    }

    // Picks up chains the UI sent through the exchange file (see state/ChainExchange.hpp)
    void watchExchange()
    {
        uint32_t seenRevision = 0;
        while (!fStopExchange)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            const auto revision = static_cast<uint32_t>(fEngine.getParameter(kParamChainRevision));
            if (revision == seenRevision)
                continue;
            seenRevision = revision;
            if (const std::optional<std::string> json = readChainExchange(fInstanceId))
                applyChain(json->c_str());
        }
    }

    Engine fEngine;
    uint32_t fReportedLatency = 0;

    uint32_t fInstanceId = 0;
    mutable std::mutex fChainMutex;
    std::string fChainJson;
    std::atomic<bool> fStopExchange { false };
    std::thread fExchangeThread;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMsayinPlugin)
};

Plugin* createPlugin()
{
    return new NAMsayinPlugin();
}

END_NAMESPACE_DISTRHO
