// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/PhaseCheck.hpp"

#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/NamBlock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace xythos {

namespace {

constexpr double kRate = 48000.0;
constexpr uint32_t kChunk = 256;
constexpr size_t kLength = 24000;   // 0.5 s of test signal
constexpr size_t kSettle = 4096;    // left out of the comparison: smoothers settling, the IRs' tails filling up
constexpr int kSearch = 24;         // samples either side of Align's delay searched for the best match (0.5 ms)
constexpr float kClear = 0.5f;      // |correlation| needed for an answer

// Quiet pink-ish noise between ~80 Hz and ~5 kHz, the same every time: the band where two amps or cabs summing out
// of phase is heard, quiet enough (about -30 dBFS) that amp models stay nearly linear
std::vector<float> testSignal()
{
    std::vector<float> x(kLength);
    uint32_t seed = 0x12345678u;
    float hp = 0.0f, lp1 = 0.0f, lp2 = 0.0f, prev = 0.0f;
    const float hpCoef = std::exp(-2.0f * 3.14159265f * 80.0f / static_cast<float>(kRate));
    const float lpCoef = 1.0f - std::exp(-2.0f * 3.14159265f * 5000.0f / static_cast<float>(kRate));
    for (float& v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        const float white = static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
        lp1 += lpCoef * (white - lp1);
        lp2 += lpCoef * (lp1 - lp2);
        hp = hpCoef * (hp + lp2 - prev); // one-pole high-pass
        prev = lp2;
        v = hp;
    }
    float peak = 0.0f;
    for (const float v : x)
        peak = std::max(peak, std::abs(v));
    for (float& v : x)
        v *= peak > 0.0f ? 0.1f / peak : 0.0f;
    return x;
}

// The branch's blocks run in series over the test signal (mono); false if one didn't load, or none take part
bool runBranch(const std::vector<PhaseBlock>& branch, std::vector<float>& signal, const std::atomic<bool>* cancel)
{
    std::vector<std::unique_ptr<Block>> blocks;
    for (const PhaseBlock& spec : branch)
    {
        if (!phaseRelevant(spec.type))
            continue;
        std::unique_ptr<Block> block = spec.type->create();
        if (!block->configure(spec.config))
            return false;
        block->prepare(kRate, kChunk);
        for (int p = 0; p < spec.type->numParams; ++p)
            block->setParam(p, spec.type->params[static_cast<size_t>(p)].toValue(spec.params[static_cast<size_t>(p)]));
        block->reset();
        blocks.push_back(std::move(block));
        if (cancel && cancel->load(std::memory_order_relaxed))
            return false;
    }
    if (blocks.empty())
        return false;
    for (size_t at = 0; at < signal.size(); at += kChunk)
    {
        float* io[1] = { signal.data() + at };
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kChunk, signal.size() - at));
        for (const std::unique_ptr<Block>& block : blocks)
            block->process(io, 1, frames);
    }
    for (const float v : signal)
        if (!std::isfinite(v))
            return false;
    return true;
}

// What two branches out of phase do to the sound is cancel each other's low end, so that's what's compared: below
// about 400 Hz (two one-pole low-passes), where small timing differences between two amps or cabs don't flip the sign
void lowPass(std::vector<float>& x)
{
    const float coef = 1.0f - std::exp(-2.0f * 3.14159265f * 400.0f / static_cast<float>(kRate));
    float s1 = 0.0f, s2 = 0.0f;
    for (float& v : x)
    {
        s1 += coef * (v - s1);
        s2 += coef * (s1 - s2);
        v = s2;
    }
}

} // namespace

bool phaseRelevant(const BlockDescriptor* type)
{
    return type == &kNamBlockDescriptor || type == &kIrBlockDescriptor;
}

PhaseResult compareBranchPolarity(const std::vector<PhaseBlock>& branchA, const std::vector<PhaseBlock>& branchB,
                                  double alignMs, const std::atomic<bool>* cancel)
{
    PhaseResult result;
    std::vector<float> a = testSignal(), b = a;
    if (!runBranch(branchA, a, cancel) || !runBranch(branchB, b, cancel))
        return result;
    lowPass(a);
    lowPass(b);

    // branch B, delayed by Align as the merge delays it, against A: the normalized correlation at each lag around it
    const int align = static_cast<int>(std::lround(alignMs * kRate / 1000.0));
    float best = 0.0f;
    for (int lag = align - kSearch; lag <= align + kSearch; ++lag)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = kSettle + kSearch * 2; i + kSearch * 2 < kLength; ++i)
        {
            const long j = static_cast<long>(i) - lag; // B's sample that lines up with A's i
            if (j < 0 || j >= static_cast<long>(kLength))
                continue;
            const double x = a[i], y = b[static_cast<size_t>(j)];
            ab += x * y;
            aa += x * x;
            bb += y * y;
        }
        if (aa <= 1e-12 || bb <= 1e-12)
            return result; // a branch is silent
        const float r = static_cast<float>(ab / std::sqrt(aa * bb));
        if (std::abs(r) > std::abs(best))
        {
            best = r;
            result.lag = lag;
        }
        if (cancel && cancel->load(std::memory_order_relaxed))
            return PhaseResult {};
    }
    result.correlation = best;
    result.polarity = best >= kClear ? Polarity::Same : best <= -kClear ? Polarity::Opposite : Polarity::Unknown;
    return result;
}

} // namespace xythos
