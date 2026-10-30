// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/Paths.hpp"

#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace xythos {

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

bool isSeparator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

// The home folder as UTF-8 without trailing separators ("" when there's none worth shortening, like "/")
std::string homeText(const std::string& home)
{
    std::string text = home.empty() ? pathToUtf8(homeDir()) : home;
    while (!text.empty() && isSeparator(text.back()))
        text.pop_back();
    return text;
}

bool samePrefix(const std::string& path, const std::string& prefix)
{
    if (path.size() < prefix.size())
        return false;
#ifdef _WIN32
    // Windows paths ignore case
    for (size_t i = 0; i < prefix.size(); ++i)
    {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(path[i]) != lower(prefix[i]) && !(isSeparator(path[i]) && isSeparator(prefix[i])))
            return false;
    }
    return true;
#else
    return path.compare(0, prefix.size(), prefix) == 0;
#endif
}

} // namespace

std::string shortenHomePath(const std::string& utf8, const std::string& home)
{
    const std::string prefix = homeText(home);
    if (prefix.empty() || !samePrefix(utf8, prefix))
        return utf8;
    if (utf8.size() == prefix.size())
        return "~";
    if (!isSeparator(utf8[prefix.size()]))
        return utf8; // "/home/me2" isn't inside "/home/me"
    return "~" + utf8.substr(prefix.size());
}

std::string expandHomePath(const std::string& utf8, const std::string& home)
{
    if (utf8.empty() || utf8[0] != '~' || (utf8.size() > 1 && !isSeparator(utf8[1])))
        return utf8; // "~other" (another user's home) stays as typed
    const std::string prefix = homeText(home);
    if (prefix.empty())
        return utf8;
    return prefix + utf8.substr(1);
}

fs::path configDir()
{
    if (auto dir = envPath("XYTHOS_CONFIG_DIR"))
        return *dir;
#ifdef _WIN32
    if (auto appData = envPath("APPDATA"))
        return *appData / "Xythos";
    return homeDir() / "AppData" / "Roaming" / "Xythos";
#elif defined(__APPLE__)
    return homeDir() / "Library" / "Application Support" / "Xythos";
#else
    if (auto xdg = envPath("XDG_CONFIG_HOME"))
        return *xdg / "xythos";
    return homeDir() / ".config" / "xythos";
#endif
}

fs::path cacheDir()
{
    if (auto dir = envPath("XYTHOS_CACHE_DIR"))
        return *dir;
#ifdef _WIN32
    if (auto local = envPath("LOCALAPPDATA"))
        return *local / "Xythos" / "Cache";
    return homeDir() / "AppData" / "Local" / "Xythos" / "Cache";
#elif defined(__APPLE__)
    return homeDir() / "Library" / "Caches" / "Xythos";
#else
    if (auto xdg = envPath("XDG_CACHE_HOME"))
        return *xdg / "xythos";
    return homeDir() / ".cache" / "xythos";
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

} // namespace xythos
