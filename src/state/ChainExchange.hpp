// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace xythos {

// A second route for the chain between UI and DSP, for hosts that don't deliver the UI's state messages to the
// DSP (Reaper's LV2 hosting). The UI writes the chain to a per-instance file and bumps the Chain Revision parameter,
// which every host delivers; the DSP reads the file when the revision changes. The DSP writes the file too, so a
// newly opened UI can read the current chain.
//
// UI and DSP always run on the same machine, so a temporary file is enough. Each plugin instance picks a random
// ID when it starts, so files from earlier sessions are never picked up.

// Path of the exchange file for a plugin instance
std::string chainExchangePath(uint32_t instanceId);

// Writes atomically (temporary file + rename). Returns false on failure.
bool writeChainExchange(uint32_t instanceId, const std::string& json);

std::optional<std::string> readChainExchange(uint32_t instanceId);

// Instance IDs and revisions travel as float parameters, so they stay below 2^24 (exact in a float)
constexpr uint32_t kMaxExchangeValue = (1u << 24) - 1;

} // namespace xythos
