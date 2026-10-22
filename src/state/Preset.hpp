// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "state/ChainState.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace namsayin {

// A preset: the chain (blocks, values, MIDI map) plus the global controls that belong to a sound.
//
// File (.namsayin): a zip with preset.json and, when saved with "embed files", every file the chain uses
// (NAM models, IRs and their sidecar images) under files/<content hash>/<file name>, so it can be shared.
struct Preset {
    static constexpr int kVersion = 1;
    static constexpr const char* kExtension = ".namsayin";

    ChainState chain;
    std::map<std::string, float> globals; // by parameter symbol (see presetGlobalParams)

    std::string toJson() const;
    static std::optional<Preset> fromJson(std::string_view json, std::string* error = nullptr);
};

// The global parameters a preset stores: input mode, input, bass, middle, treble, output, limiter and the
// split's merge controls
const std::vector<uint32_t>& presetGlobalParams();

bool savePresetFile(const std::string& path, const Preset& preset, bool embedFiles, std::string* error = nullptr);

// How a block's file was found when loading a preset
enum class FileSource { Original, Embedded, Library, Missing };

struct LoadedPreset {
    Preset preset;
    std::vector<FileSource> sources; // one per block (Original for blocks without a file)
};

// Loads a preset file and resolves each block's file: the original path if it still exists, else the embedded
// copy (extracted to the cache directory), else a library file with the same content hash (`findByHash` returns
// its path or ""), else the block keeps its path and is reported Missing.
std::optional<LoadedPreset> loadPresetFile(const std::string& path,
                                           const std::function<std::string(const std::string& hash)>& findByHash,
                                           std::string* error = nullptr);

} // namespace namsayin
