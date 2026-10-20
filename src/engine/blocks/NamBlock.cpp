// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/NamBlock.hpp"

#include "engine/dsp/Smoother.hpp"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace namsayin {

namespace {

class NamBlock : public Block {
public:
    void prepare(double sampleRate, uint32_t maxFrames) override
    {
        fSampleRate = sampleRate;
        fMaxFrames = maxFrames;
        for (std::vector<float>& buffer : fOut)
            buffer.assign(maxFrames, 0.0f);
        fInputGain.prepare(sampleRate);
        fOutputGain.prepare(sampleRate);
        fInputGain.snap(dbToGain(fInputDb));
        fOutputGain.snap(targetOutputGain());
        resetModels();
    }

    bool configure(const nlohmann::json& config) override
    {
        const std::string path = config.value("path", std::string());
        const double a2Size = config.value("a2Size", 1.0);
        if (path.empty())
            return false;

        try
        {
            // One model per channel, because models keep state. In mono mode only the first runs.
            for (auto& model : fModels)
            {
                model = nam::get_dsp(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())));
                if (!model || model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
                    return false;
                if (auto* slimmable = dynamic_cast<nam::SlimmableModel*>(model.get()))
                    slimmable->SetSlimmableSize(a2Size);
            }
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "NAMsayin: can't load %s: %s\n", path.c_str(), e.what());
            return false;
        }

        fLoudnessDb = fModels[0]->HasLoudness() ? fModels[0]->GetLoudness() : 0.0;
        fHasLoudness = fModels[0]->HasLoudness();
        resetModels();
        fOutputGain.snap(targetOutputGain());
        return true;
    }

    void setParam(int index, float value) override
    {
        switch (index)
        {
        case kNamParamInput:
            fInputDb = value;
            fInputGain.setTarget(dbToGain(value));
            break;
        case kNamParamOutput:
            fOutputDb = value;
            fOutputGain.setTarget(targetOutputGain());
            break;
        case kNamParamNormalize:
            fNormalize = value >= 0.5f;
            fOutputGain.setTarget(targetOutputGain());
            break;
        }
    }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        if (!fModels[0])
            return;

        // Gains are smoothed once per buffer for both channels, so they stay identical in stereo
        const Smoother inputStart = fInputGain;
        const Smoother outputStart = fOutputGain;
        for (uint32_t ch = 0; ch < numChannels; ++ch)
        {
            fInputGain = inputStart;
            fOutputGain = outputStart;

            float* x = io[ch];
            for (uint32_t i = 0; i < frames; ++i)
                x[i] *= fInputGain.next();

            NAM_SAMPLE* in[1] = { x };
            NAM_SAMPLE* out[1] = { fOut[ch].data() };
            fModels[ch]->process(in, out, static_cast<int>(frames));

            const float* y = fOut[ch].data();
            for (uint32_t i = 0; i < frames; ++i)
                x[i] = y[i] * fOutputGain.next();
        }
    }

    void reset() override { resetModels(); }

private:
    float targetOutputGain() const
    {
        float db = fOutputDb;
        if (fNormalize && fHasLoudness)
            db += static_cast<float>(kNamTargetLoudnessDb - fLoudnessDb);
        return dbToGain(db);
    }

    void resetModels()
    {
        for (auto& model : fModels)
        {
            if (model && fMaxFrames > 0)
                model->Reset(fSampleRate, static_cast<int>(fMaxFrames));
        }
    }

    std::unique_ptr<nam::DSP> fModels[2];
    std::vector<float> fOut[2];
    double fSampleRate = 48000.0;
    uint32_t fMaxFrames = 0;
    double fLoudnessDb = 0.0;
    bool fHasLoudness = false;
    float fInputDb = 0.0f;
    float fOutputDb = 0.0f;
    bool fNormalize = true;
    Smoother fInputGain, fOutputGain;
};

} // namespace

const BlockDescriptor kNamBlockDescriptor = {
    "nam",
    "Local NAM",
    Category::LocalNam,
    kNamParamCount,
    {
        ParamSpec { "Input", "dB", -24.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Output", "dB", -24.0f, 24.0f, 0.0f, ParamScale::Linear },
        ParamSpec { "Normalize", "", 0.0f, 1.0f, 1.0f, ParamScale::Toggle },
    },
    [] { return std::unique_ptr<Block>(new NamBlock()); },
};

} // namespace namsayin
