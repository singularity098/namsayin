// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace namsayin {

// Per-user directories. Linux: $XDG_CONFIG_HOME/namsayin (~/.config/namsayin) and $XDG_CACHE_HOME/namsayin
// (~/.cache/namsayin). Windows: %APPDATA%\NAMsayin and %LOCALAPPDATA%\NAMsayin\Cache.
// NAMSAYIN_CONFIG_DIR and NAMSAYIN_CACHE_DIR override them (used by the tests).
std::filesystem::path configDir();
std::filesystem::path cacheDir();

// Paths travel as UTF-8 strings (state, JSON); these convert without going through the system code page
std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string pathToUtf8(const std::filesystem::path& path);

// Writes through a temporary file and a rename, so readers never see a half-written file
bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents);
std::optional<std::string> readFile(const std::filesystem::path& path);

} // namespace namsayin
