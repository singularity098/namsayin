// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace namsayin {

// Global per-user settings and favorites, shared by every plugin instance: one JSON file in configDir().
// Every open UI reloads it when it changes on disk.
struct Settings {
    static constexpr int kVersion = 1;

    std::string namDir;          // library folders (UTF-8), empty if not set
    std::string irDir;
    bool a2Lite = false;         // default A2 size for new NAM blocks
    bool normalize = true;       // default normalization for new NAM and IR blocks
    bool parallel = true;        // run split branch B on a second core
    int accent = 0;              // index into the UI's accent colours
    std::set<std::string> favorites; // content hashes (XXH3-128, hex)
    bool embedFiles = true;      // exported presets carry the files they use (saved ones never do)
    std::string defaultPreset;   // loaded into new plugin instances; empty for none
    std::vector<std::string> recentPresets; // most recent first

    static constexpr size_t kMaxRecentPresets = 8;
    void addRecentPreset(const std::string& path);

    std::string toJson() const;
    // Missing or invalid fields keep their defaults
    static Settings fromJson(std::string_view json);

    static std::filesystem::path defaultPath();
    static Settings load(const std::filesystem::path& path = defaultPath());
    bool save(const std::filesystem::path& path = defaultPath()) const;

    bool operator==(const Settings&) const = default;
};

} // namespace namsayin
