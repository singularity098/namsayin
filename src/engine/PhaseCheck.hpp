// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

#include <array>
#include <atomic>
#include <vector>

#include <nlohmann/json.hpp>

namespace xythos {

// Whether the two branches of a split are out of phase with each other (owner, 2026-10-31): two amps or two cab IRs
// whose polarity differs cancel each other's low end when merged, which the merge's Invert A/B fixes.
//
// Not real time and never on the audio thread: the UI runs it on a thread of its own, once each time what matters
// changes (the NAM and IR blocks in the branches, their settings, Align). It makes private copies of each branch's NAM
// and IR blocks (the other blocks are left out: they rarely flip polarity, and time-varying ones would blur the
// comparison), plays the same quiet, band-limited test signal through each branch, and correlates the two outputs' low end
// (below about 400 Hz, where out-of-phase branches cancel) near the delay Align sets. It costs one model load per NAM block and well under a second of CPU, so the user doesn't
// notice it.
struct PhaseBlock {
    const BlockDescriptor* type = nullptr;
    nlohmann::json config;
    std::array<float, kParamsPerSlot> params {}; // normalized, as the host holds them
};

enum class Polarity {
    Unknown, // nothing to compare (a branch without a NAM or IR block, a file that didn't load), or no clear answer
    Same,    // the branches are in phase
    Opposite // one branch is the other's polarity flipped
};

struct PhaseResult {
    Polarity polarity = Polarity::Unknown;
    float correlation = 0.0f; // at the best lag, -1..1
    int lag = 0;              // samples branch B is late against A, at that lag
};

// The branches' polarity, before the merge's Invert switches. `alignMs` is the merge's Align (> 0 delays branch B).
// `cancel`, when set, stops it early (returns Unknown).
PhaseResult compareBranchPolarity(const std::vector<PhaseBlock>& branchA, const std::vector<PhaseBlock>& branchB,
                                  double alignMs, const std::atomic<bool>* cancel = nullptr);

// Whether a block type takes part (NAM and IR blocks)
bool phaseRelevant(const BlockDescriptor* type);

} // namespace xythos
