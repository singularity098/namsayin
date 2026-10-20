// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/Engine.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace namsayin {

namespace {

constexpr uint32_t kDefaultMaxFrames = 2048;

float clampDb(float db)
{
    return std::clamp(db, -60.0f, 6.0f);
}

} // namespace

Engine::Engine(const BlockRegistry& registry)
    : fRegistry(registry)
{
    for (uint32_t i = 0; i < kParamCount; ++i)
        fParams[i].store(0.0f, std::memory_order_relaxed);
    for (uint32_t i = 0; i < kGlobalParamCount; ++i)
        fParams[i].store(globalParamSpec(i).def, std::memory_order_relaxed);

    fActive = new Graph();
    prepare(fSampleRate, kDefaultMaxFrames);
}

Engine::~Engine()
{
    drainRetired();
    delete fPending.exchange(nullptr);
    delete fNext;
    delete fActive;
}

void Engine::prepare(double sampleRate, uint32_t maxFrames)
{
    const std::lock_guard<std::mutex> lock(fMutex);

    fSampleRate = sampleRate;
    fMaxFrames = std::max<uint32_t>(maxFrames, 1);

    // Audio is stopped, so finish any pending swap right away
    drainRetired();
    if (Graph* pending = fPending.exchange(nullptr))
    {
        delete fNext;
        fNext = pending;
    }
    if (fNext)
    {
        delete fActive;
        fActive = fNext;
        fNext = nullptr;
    }
    fFade = Fade::Idle;
    fFadeGain = 1.0f;

    std::unordered_set<Block*> blocks;
    for (const Node& node : fActive->nodes)
        blocks.insert(node.block.get());
    for (const CachedBlock& cached : fCache)
    {
        if (cached.block)
            blocks.insert(cached.block.get());
    }
    for (Block* block : blocks)
    {
        block->prepare(fSampleRate, fMaxFrames);
        block->reset();
    }

    fLatency.store(fActive->latency, std::memory_order_relaxed);
    for (SlotState& slot : fSlots)
        slot = SlotState {};
    for (const Node& node : fActive->nodes)
    {
        SlotState& slot = fSlots[node.slot];
        slot.block = node.block.get();
        slot.applyAll = true;
        slot.bypassMix = param(slotBypassParam(node.slot)) >= 0.5f ? 1.0f : 0.0f;
    }

    fFadeStep = static_cast<float>(1.0 / (kSwapFadeSeconds * fSampleRate));
    fRampStep = static_cast<float>(1.0 / (kBypassRampSeconds * fSampleRate));
    fGlobalBypassMix = param(kParamBypass) >= 0.5f ? 1.0f : 0.0f;

    fInputGain.prepare(fSampleRate);
    fInputGain.snap(dbToGain(param(kParamInput)));
    fOutputGain.prepare(fSampleRate);
    fOutputGain.snap(dbToGain(param(kParamOutput)));
    fToneStack.setGains(param(kParamBass), param(kParamMiddle), param(kParamTreble));
    fToneStack.prepare(fSampleRate);
    fLimiter.prepare(fSampleRate);
    for (int ch = 0; ch < 2; ++ch)
    {
        fInMeters[ch].prepare(fSampleRate);
        fOutMeters[ch].prepare(fSampleRate);
        fWork[ch].assign(fMaxFrames, 0.0f);
        fDry[ch].assign(fMaxFrames, 0.0f);
        fScratch[ch].assign(fMaxFrames, 0.0f);
    }
}

bool Engine::setChain(const ChainState& chain, std::string* error, std::vector<int>* failedSlots)
{
    if (!chain.validate(fRegistry, error))
        return false;

    const std::lock_guard<std::mutex> lock(fMutex);
    drainRetired();

    auto graph = std::make_unique<Graph>();
    std::array<CachedBlock, kMaxBlocks> cache;

    for (const BlockState& state : chain.blocks)
    {
        const BlockDescriptor* type = fRegistry.find(state.typeId);
        std::string config = state.config.dump();

        CachedBlock& previous = fCache[state.slot];
        std::shared_ptr<Block> block;
        if (previous.block && previous.typeId == state.typeId && previous.config == config)
        {
            block = previous.block;
        }
        else
        {
            block = type->create();
            block->prepare(fSampleRate, fMaxFrames);
            if (!block->configure(state.config))
            {
                if (failedSlots)
                    failedSlots->push_back(state.slot);
                continue;
            }
        }

        graph->latency += block->latency();
        graph->nodes.push_back({ block, type, state.slot });
        cache[state.slot] = { std::move(block), state.typeId, std::move(config) };
    }

    fCache = std::move(cache);

    // A graph still pending was never seen by the audio thread, so it can be deleted here
    delete fPending.exchange(graph.release(), std::memory_order_acq_rel);
    return true;
}

void Engine::setParameter(uint32_t index, float value)
{
    if (index < kParamCount)
        fParams[index].store(value, std::memory_order_relaxed);
}

float Engine::getParameter(uint32_t index) const
{
    return index < kParamCount ? param(index) : 0.0f;
}

void Engine::process(const float* const* inputs, float* const* outputs, uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames)
    {
        const uint32_t n = std::min(frames - done, fMaxFrames);
        const float* in[2] = { inputs[0] + done, inputs[1] + done };
        float* out[2] = { outputs[0] + done, outputs[1] + done };
        processChunk(in, out, n);
        done += n;
    }

    fParams[kParamInPeakL].store(clampDb(gainToDb(fInMeters[0].level())), std::memory_order_relaxed);
    fParams[kParamInPeakR].store(clampDb(gainToDb(fInMeters[1].level())), std::memory_order_relaxed);
    fParams[kParamOutPeakL].store(clampDb(gainToDb(fOutMeters[0].level())), std::memory_order_relaxed);
    fParams[kParamOutPeakR].store(clampDb(gainToDb(fOutMeters[1].level())), std::memory_order_relaxed);
    fParams[kParamInClip].store(fInMeters[0].clipping() || fInMeters[1].clipping() ? 1.0f : 0.0f,
                                std::memory_order_relaxed);
    fParams[kParamOutClip].store(fOutMeters[0].clipping() || fOutMeters[1].clipping() ? 1.0f : 0.0f,
                                 std::memory_order_relaxed);
}

void Engine::processChunk(const float* const* inputs, float* const* outputs, uint32_t frames)
{
    const bool stereo = param(kParamInputMode) >= 0.5f;
    const uint32_t numChannels = stereo ? 2 : 1;

    // Copy first: inputs and outputs may be the same buffers
    for (int ch = 0; ch < 2; ++ch)
        std::memcpy(fDry[ch].data(), inputs[ch], sizeof(float) * frames);

    // Input stage
    fInputGain.setTarget(dbToGain(param(kParamInput)));
    for (uint32_t i = 0; i < frames; ++i)
    {
        const float g = fInputGain.next();
        fWork[0][i] = fDry[0][i] * g;
        fWork[1][i] = fDry[1][i] * g;
    }
    fInMeters[0].process(fWork[0].data(), frames);
    fInMeters[1].process(fWork[stereo ? 1 : 0].data(), frames);

    // Chain, with a fade out/in around graph swaps
    if (fFade == Fade::Idle)
    {
        if (Graph* pending = fPending.exchange(nullptr, std::memory_order_acq_rel))
        {
            fNext = pending;
            fFade = Fade::Out;
        }
    }

    processChain(numChannels, frames);

    if (fFade != Fade::Idle)
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (fFade == Fade::Out)
            {
                fFadeGain = std::max(0.0f, fFadeGain - fFadeStep);
            }
            else if (fFade == Fade::In)
            {
                fFadeGain = std::min(1.0f, fFadeGain + fFadeStep);
                if (fFadeGain >= 1.0f)
                    fFade = Fade::Idle;
            }
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                fWork[ch][i] *= fFadeGain;
        }
        if (fFade == Fade::Out && fFadeGain <= 0.0f)
        {
            swapGraph();
            fFade = Fade::In;
        }
    }

    // Output stage
    float* work[2] = { fWork[0].data(), fWork[1].data() };
    fToneStack.setGains(param(kParamBass), param(kParamMiddle), param(kParamTreble));
    fToneStack.process(work, numChannels, frames);

    fOutputGain.setTarget(dbToGain(param(kParamOutput)));
    for (uint32_t i = 0; i < frames; ++i)
    {
        const float g = fOutputGain.next();
        for (uint32_t ch = 0; ch < numChannels; ++ch)
            fWork[ch][i] *= g;
    }

    float reduction = 1.0f;
    if (param(kParamLimiter) >= 0.5f)
        reduction = fLimiter.process(work, numChannels, frames);
    else
        fLimiter.reset();
    fParams[kParamLimiterReduction].store(-gainToDb(reduction, -24.0f), std::memory_order_relaxed);

    if (!stereo)
        std::memcpy(fWork[1].data(), fWork[0].data(), sizeof(float) * frames);

    // Global bypass: crossfade to the untouched input
    const float bypassTarget = param(kParamBypass) >= 0.5f ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < frames; ++i)
    {
        if (fGlobalBypassMix < bypassTarget)
            fGlobalBypassMix = std::min(bypassTarget, fGlobalBypassMix + fRampStep);
        else if (fGlobalBypassMix > bypassTarget)
            fGlobalBypassMix = std::max(bypassTarget, fGlobalBypassMix - fRampStep);

        for (int ch = 0; ch < 2; ++ch)
        {
            // exact at the ends, so fully bypassed really is the untouched input
            if (fGlobalBypassMix == 0.0f)
                outputs[ch][i] = fWork[ch][i];
            else if (fGlobalBypassMix == 1.0f)
                outputs[ch][i] = fDry[ch][i];
            else
                outputs[ch][i] = fWork[ch][i] + (fDry[ch][i] - fWork[ch][i]) * fGlobalBypassMix;
        }
    }

    fOutMeters[0].process(outputs[0], frames);
    fOutMeters[1].process(outputs[1], frames);
}

void Engine::processChain(uint32_t numChannels, uint32_t frames)
{
    float* work[2] = { fWork[0].data(), fWork[1].data() };
    float* scratch[2] = { fScratch[0].data(), fScratch[1].data() };

    for (const Node& node : fActive->nodes)
    {
        SlotState& slot = fSlots[node.slot];
        Block& block = *node.block;

        for (int p = 0; p < node.type->numParams; ++p)
        {
            const float normalized = param(slotParam(node.slot, p));
            if (slot.applyAll || normalized != slot.applied[p])
            {
                block.setParam(p, node.type->params[p].toValue(normalized));
                slot.applied[p] = normalized;
            }
        }
        slot.applyAll = false;

        const float bypassTarget = param(slotBypassParam(node.slot)) >= 0.5f ? 1.0f : 0.0f;
        if (slot.bypassMix == bypassTarget)
        {
            if (bypassTarget == 0.0f)
                block.process(work, numChannels, frames);
            continue;
        }

        // Ramping between bypassed and active: process a copy and crossfade it with the dry signal
        for (uint32_t ch = 0; ch < numChannels; ++ch)
            std::memcpy(scratch[ch], work[ch], sizeof(float) * frames);
        block.process(scratch, numChannels, frames);

        float mix = slot.bypassMix;
        for (uint32_t i = 0; i < frames; ++i)
        {
            mix = mix < bypassTarget ? std::min(bypassTarget, mix + fRampStep)
                                     : std::max(bypassTarget, mix - fRampStep);
            for (uint32_t ch = 0; ch < numChannels; ++ch)
                work[ch][i] = scratch[ch][i] + (work[ch][i] - scratch[ch][i]) * mix;
        }
        slot.bypassMix = mix;
    }
}

void Engine::swapGraph()
{
    Graph* old = fActive;
    fActive = fNext;
    fNext = nullptr;

    for (const Node& node : fActive->nodes)
    {
        SlotState& slot = fSlots[node.slot];
        if (slot.block != node.block.get())
        {
            // A new block in this slot: apply all its parameters and start at its bypass state, no ramp
            slot.block = node.block.get();
            slot.applyAll = true;
            slot.bypassMix = param(slotBypassParam(node.slot)) >= 0.5f ? 1.0f : 0.0f;
        }
    }

    fLatency.store(fActive->latency, std::memory_order_relaxed);

    // Freed on a non-RT thread. The queue can't fill up in practice (each swap follows one setChain(), which
    // drains it); if it ever does, leaking is better than freeing on the audio thread.
    fRetired.push(old);
}

void Engine::drainRetired()
{
    Graph* graph;
    while (fRetired.pop(graph))
        delete graph;
}

} // namespace namsayin
