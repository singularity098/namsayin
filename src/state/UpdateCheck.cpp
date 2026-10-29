// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/UpdateCheck.hpp"

#include "state/Paths.hpp"

#include <nlohmann/json.hpp>

namespace xythos {

using nlohmann::json;

std::optional<Version> parseVersion(std::string_view text)
{
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    Version version {};
    size_t part = 0;
    size_t i = 0;
    while (true)
    {
        // one number: digits only, at most 6 of them
        const size_t start = i;
        int value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && i - start < 6)
            value = value * 10 + (text[i++] - '0');
        if (i == start)
            return std::nullopt;
        version[part++] = value;
        if (i == text.size())
            return version;
        if (text[i] != '.' || part == version.size())
            return std::nullopt;
        ++i;
    }
}

std::string versionString(const Version& version)
{
    return std::to_string(version[0]) + "." + std::to_string(version[1]) + "." + std::to_string(version[2]);
}

bool isNewerVersion(std::string_view latest, std::string_view current)
{
    const std::optional<Version> a = parseVersion(latest), b = parseVersion(current);
    return a && b && *a > *b;
}

std::optional<std::string> releaseVersion(std::string_view text)
{
    try
    {
        const json root = json::parse(text);
        if (!root.is_object() || root.value("draft", false) || root.value("prerelease", false))
            return std::nullopt;
        const auto tag = root.find("tag_name");
        if (tag == root.end() || !tag->is_string())
            return std::nullopt;
        if (const std::optional<Version> version = parseVersion(tag->get<std::string>()))
            return versionString(*version);
    }
    catch (const json::exception&)
    {
    }
    return std::nullopt;
}

std::string UpdateCache::toJson() const
{
    return json { { "checkedAt", checkedAt }, { "latest", latest } }.dump(2);
}

UpdateCache UpdateCache::fromJson(std::string_view text)
{
    UpdateCache cache;
    try
    {
        const json root = json::parse(text);
        if (!root.is_object())
            return cache;
        if (const auto it = root.find("checkedAt"); it != root.end() && it->is_number_integer())
            cache.checkedAt = it->get<int64_t>();
        if (const auto it = root.find("latest"); it != root.end() && it->is_string())
            if (const std::optional<Version> version = parseVersion(it->get<std::string>()))
                cache.latest = versionString(*version);
    }
    catch (const json::exception&)
    {
    }
    return cache;
}

std::filesystem::path UpdateCache::defaultPath()
{
    return cacheDir() / "update-check.json";
}

UpdateCache UpdateCache::load(const std::filesystem::path& path)
{
    const std::optional<std::string> text = readFile(path);
    return text ? fromJson(*text) : UpdateCache {};
}

bool UpdateCache::save(const std::filesystem::path& path) const
{
    return writeFileAtomic(path, toJson());
}

std::optional<std::string> updateToShow(const UpdateCache& cache, std::string_view current,
                                        std::string_view dismissed)
{
    if (!isNewerVersion(cache.latest, current))
        return std::nullopt;
    // a dismissed version stays hidden, and so do older ones; a newer one shows again
    if (const std::optional<Version> hidden = parseVersion(dismissed))
        if (*parseVersion(cache.latest) <= *hidden)
            return std::nullopt;
    return cache.latest;
}

UpdateChecker::UpdateChecker(std::unique_ptr<HttpFetcher> fetcher, std::filesystem::path cachePath)
    : fFetcher(std::move(fetcher))
    , fCachePath(std::move(cachePath))
{
}

UpdateChecker::~UpdateChecker()
{
    stop();
}

bool UpdateChecker::start(int64_t now)
{
    if (fBusy.load())
        return false;
    if (fThread.joinable())
        fThread.join(); // the last check's thread, already finished
    UpdateCache cache = UpdateCache::load(fCachePath);
    if (!cache.checkDue(now))
        return false;
    // claim this check, so other instances opening now don't make their own (a failed check waits a day too)
    cache.checkedAt = now;
    cache.save(fCachePath);
    fBusy = true;
    fThread = std::thread([this, now] {
        // a release's JSON is a few KB, more with long notes
        if (const std::optional<std::string> body = fFetcher->get(kLatestReleaseApiUrl, 1 << 20))
            if (const std::optional<std::string> latest = releaseVersion(*body))
                UpdateCache { now, *latest }.save(fCachePath);
        fFinished = true;
        fBusy = false;
    });
    return true;
}

void UpdateChecker::stop()
{
    if (!fThread.joinable())
        return;
    if (fBusy.load())
        fFetcher->cancel();
    fThread.join();
}

} // namespace xythos
