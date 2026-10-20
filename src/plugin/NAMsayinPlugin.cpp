// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoPlugin.hpp"

#include "NAM/version.h"

#include <cstring>

START_NAMESPACE_DISTRHO

// M0 skeleton: a stereo pass-through. The engine (docs/Architecture.md, section 4) replaces run() from M1 on.
class NAMsayinPlugin : public Plugin
{
public:
    NAMsayinPlugin()
        : Plugin(0, 0, 0)
    {
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

    void run(const float** const inputs, float** const outputs, const uint32_t frames) override
    {
        for (uint32_t ch = 0; ch < DISTRHO_PLUGIN_NUM_OUTPUTS; ++ch)
        {
            if (outputs[ch] != inputs[ch])
                std::memcpy(outputs[ch], inputs[ch], sizeof(float) * frames);
        }
    }

private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMsayinPlugin)
};

Plugin* createPlugin()
{
    return new NAMsayinPlugin();
}

END_NAMESPACE_DISTRHO
