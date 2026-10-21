// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/Paths.hpp"

#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace namsayin {

namespace fs = std::filesystem;

namespace {

std::optional<fs::path> envPath(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return std::nullopt;
    return pathFromUtf8(value);
}

fs::path homeDir()
{
    if (auto home = envPath("HOME"))
        return *home;
    if (auto profile = envPath("USERPROFILE"))
        return *profile;
    return fs::current_path();
}

} // namespace

fs::path configDir()
{
    if (auto dir = envPath("NAMSAYIN_CONFIG_DIR"))
        return *dir;
#ifdef _WIN32
    if (auto appData = envPath("APPDATA"))
        return *appData / "NAMsayin";
    return homeDir() / "AppData" / "Roaming" / "NAMsayin";
#else
    if (auto xdg = envPath("XDG_CONFIG_HOME"))
        return *xdg / "namsayin";
    return homeDir() / ".config" / "namsayin";
#endif
}

fs::path cacheDir()
{
    if (auto dir = envPath("NAMSAYIN_CACHE_DIR"))
        return *dir;
#ifdef _WIN32
    if (auto local = envPath("LOCALAPPDATA"))
        return *local / "NAMsayin" / "Cache";
    return homeDir() / "AppData" / "Local" / "NAMsayin" / "Cache";
#else
    if (auto xdg = envPath("XDG_CACHE_HOME"))
        return *xdg / "namsayin";
    return homeDir() / ".cache" / "namsayin";
#endif
}

fs::path pathFromUtf8(const std::string& utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string pathToUtf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool writeFileAtomic(const fs::path& path, const std::string& contents)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // a unique temporary name, so two instances saving at once don't write into the same file
    std::random_device random;
    fs::path temp = path;
    temp += ".tmp" + std::to_string(random());
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << contents;
        if (!out)
        {
            out.close();
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, path, ec);
    if (ec)
    {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

std::optional<std::string> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace namsayin
