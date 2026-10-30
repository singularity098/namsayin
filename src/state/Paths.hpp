// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace xythos {

// Per-user directories. Linux: $XDG_CONFIG_HOME/xythos (~/.config/xythos) and $XDG_CACHE_HOME/xythos
// (~/.cache/xythos). Windows: %APPDATA%\Xythos and %LOCALAPPDATA%\Xythos\Cache.
// XYTHOS_CONFIG_DIR and XYTHOS_CACHE_DIR override them (used by the tests).
std::filesystem::path configDir();
std::filesystem::path cacheDir();

// Paths travel as UTF-8 strings (state, JSON); these convert without going through the system code page
std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string pathToUtf8(const std::filesystem::path& path);

// Paths as the UI shows them: the home folder as "~" ("/home/me/NAM" -> "~/NAM"), and back for paths typed that
// way. With `home` empty they use the user's home folder ($HOME, else %USERPROFILE%).
std::string shortenHomePath(const std::string& utf8, const std::string& home = {});
std::string expandHomePath(const std::string& utf8, const std::string& home = {});

// Writes through a temporary file and a rename, so readers never see a half-written file
bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents);
std::optional<std::string> readFile(const std::filesystem::path& path);

} // namespace xythos
