// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/BlockRegistry.hpp"
#include "engine/Params.hpp"
#include "engine/ParallelWorker.hpp"
#include "engine/SpscQueue.hpp"
#include "engine/dsp/DelayLine.hpp"
#include "engine/dsp/Limiter.hpp"
#include "engine/dsp/Meter.hpp"
#include "engine/dsp/Smoother.hpp"
#include "engine/dsp/ToneStack.hpp"
#include "engine/metronome/Metronome.hpp"
#include "engine/tuner/Tuner.hpp"
#include "state/ChainState.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace namsayin {

// The audio engine: input stage, signal chain (with an optional split), tone stack, output stage, metronome,
// limiter and meters, and the tuner's input.
//
// Threads: process() runs on the audio thread and never allocates, locks or frees. setChain() and prepare()
// run on other threads; a new chain is built there and handed to the audio thread lock-free, which swaps it in
// with a short fade out/in. Blocks that didn't change are reused, so their state carries over.
class Engine {
public:
    static constexpr double kSwapFadeSeconds = 0.005;   // each of fade-out and fade-in
    static constexpr double kBypassRampSeconds = 0.005;

    explicit Engine(const BlockRegistry& registry = BlockRegistry::builtin());
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Non-RT, audio stopped
    void prepare(double sampleRate, uint32_t maxFrames);

    // Non-RT. Builds the chain and queues it for the audio thread. Blocks whose configure() fails are left out
    // (audio passes through them); their slots are listed in `failedSlots`.
    bool setChain(const ChainState& chain, std::string* error = nullptr, std::vector<int>* failedSlots = nullptr);

    // Any thread. Index from Params.hpp; slot parameters are normalized 0..1.
    void setParameter(uint32_t index, float value);
    float getParameter(uint32_t index) const;

    // RT. Two inputs and two outputs; they may alias. `transport` is the host's, for the metronome's DAW sync.
    void process(const float* const* inputs, float* const* outputs, uint32_t frames,
                 const Transport* transport = nullptr);

    // Total latency of the active chain, in samples
    uint32_t latency() const { return fLatency.load(std::memory_order_relaxed); }

    // Non-RT, any one thread. Runs the tuner's analysis on the latest input and publishes the result in the
    // tuner output parameters. Call it every 20 ms or so while the tuner is on.
    void analyzeTuner();

    // Any thread. Whether branch B of a split runs on a second core (default on). Takes effect from the next
    // setChain() if no worker thread was started yet.
    void setParallelProcessing(bool enabled) { fParallel.store(enabled, std::memory_order_relaxed); }

private:
    struct Node {
        std::shared_ptr<Block> block;
        const BlockDescriptor* type;
        int slot;
    };

    // The chain in four sections: before the split, the two branches, and after the merge. Without a split,
    // everything is in `pre`.
    struct Graph {
        std::vector<Node> pre, branchA, branchB, post;
        bool split = false;
        uint32_t latency = 0;
        DelayLine alignA, alignB;        // delays the branch with less latency so both arrive together
        ParallelWorker* worker = nullptr; // runs branch B, if parallel processing is on

        template <typename F>
        void forEachNode(F&& f) const
        {
            for (const std::vector<Node>* nodes : { &pre, &branchA, &branchB, &post })
                for (const Node& node : *nodes)
                    f(node);
        }
    };

    // What the non-RT side last built for each slot, so unchanged blocks can be reused
    struct CachedBlock {
        std::shared_ptr<Block> block;
        std::string typeId;
        std::string config;
    };

    // Audio-thread state per slot; slots outlive graphs
    struct SlotState {
        const Block* block = nullptr;
        std::array<float, kParamsPerSlot> applied {};
        bool applyAll = true;   // push every parameter on the next buffer (new block in this slot)
        float bypassMix = 0.0f; // 0 = active, 1 = bypassed
    };

    enum class Fade { Idle, Out, In };

    void processChunk(const float* const* inputs, float* const* outputs, uint32_t frames,
                      const Transport* transport, uint32_t offset);
    Metronome::Settings metronomeSettings() const;
    void processChain(uint32_t numChannels, uint32_t frames);
    void processNodes(const std::vector<Node>& nodes, float* const* work, float* const* scratch,
                      uint32_t numChannels, uint32_t frames);
    void merge(uint32_t numChannels, uint32_t frames);
    float mergeGain(int branch, int channel, bool stereo) const;
    static void runBranchB(void* engine);
    void updateLatency(Graph& graph);
    void swapGraph();
    void drainRetired();
    float param(uint32_t index) const { return fParams[index].load(std::memory_order_relaxed); }

    const BlockRegistry& fRegistry;
    std::mutex fMutex; // non-RT callers only
    std::mutex fTunerMutex; // analyzeTuner() vs prepare()

    double fSampleRate = 48000.0;
    uint32_t fMaxFrames = 0;

    std::array<std::atomic<float>, kParamCount> fParams;

    // non-RT side
    std::array<CachedBlock, kMaxBlocks> fCache;

    // handover
    std::atomic<Graph*> fPending { nullptr };
    SpscQueue<Graph*, 8> fRetired;
    std::atomic<uint32_t> fLatency { 0 };
    std::atomic<bool> fParallel { true };
    std::unique_ptr<ParallelWorker> fWorker; // started when a chain first needs it

    // audio thread side
    Graph* fActive = nullptr;
    Graph* fNext = nullptr;
    Fade fFade = Fade::Idle;
    float fFadeGain = 1.0f;
    float fFadeStep = 1.0f;
    float fRampStep = 1.0f;
    float fGlobalBypassMix = 0.0f;
    float fTunerMuteMix = 0.0f; // 1 = guitar muted by the tuner
    bool fTunerOn = false;
    std::array<SlotState, kMaxBlocks> fSlots {};

    Smoother fInputGain, fOutputGain;
    Smoother fMergeGain[2][2]; // [branch][channel]
    uint32_t fJobChannels = 0, fJobFrames = 0; // branch B job, for the worker
    ToneStack fToneStack;
    Limiter fLimiter;
    Tuner fTuner;
    Metronome fMetronome;
    Meter fInMeters[2], fOutMeters[2];

    std::vector<float> fWork[2], fDry[2], fScratch[2];
    std::vector<float> fBranch[2], fBranchScratch[2]; // branch B of a split
    std::vector<float> fClick;
};

} // namespace namsayin
