// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace xythos {

// The update check: at most once a day, the UI asks GitHub for Xythos's latest release and shows a banner when it's
// newer than this build. Nothing is downloaded or installed; the banner opens the release page. Off in Settings.

// This build's version, from the project version in CMakeLists.txt
inline constexpr const char* kAppVersion = XYTHOS_VERSION;
inline constexpr const char* kLatestReleaseApiUrl =
    "https://api.github.com/repos/singularity098/xythos/releases/latest";
inline constexpr const char* kLatestReleasePageUrl = "https://github.com/singularity098/xythos/releases/latest";

// major.minor.patch; "v1.2" and "1.2.0" are the same version. Anything else (pre-release suffixes, other text) is not
// a version, so it never shows a banner.
using Version = std::array<int, 3>;
std::optional<Version> parseVersion(std::string_view text);
std::string versionString(const Version& version);
// Whether latest is a newer version than current (false if either isn't a version)
bool isNewerVersion(std::string_view latest, std::string_view current);

// The version of a release in GitHub's API JSON (its tag), or nothing for drafts, pre-releases and anything unexpected
std::optional<std::string> releaseVersion(std::string_view json);

// The last check, shared by every instance: one small JSON file in cacheDir()
struct UpdateCache {
    int64_t checkedAt = 0; // seconds since the epoch; also set when a check starts, so instances don't all check
    std::string latest;    // the latest release's version, empty until a check succeeds

    static constexpr int64_t kInterval = 24 * 60 * 60;
    bool checkDue(int64_t now) const { return now - checkedAt >= kInterval || now < checkedAt; }

    std::string toJson() const;
    static UpdateCache fromJson(std::string_view json);
    static std::filesystem::path defaultPath();
    static UpdateCache load(const std::filesystem::path& path = defaultPath());
    bool save(const std::filesystem::path& path = defaultPath()) const;

    bool operator==(const UpdateCache&) const = default;
};

// The version the banner offers, if any: newer than this build and not dismissed
std::optional<std::string> updateToShow(const UpdateCache& cache, std::string_view current,
                                        std::string_view dismissed);

// An HTTPS GET that another thread can cancel (the platform's, in src/ui/HttpGet.cpp; tests use their own)
class HttpFetcher {
public:
    virtual ~HttpFetcher() = default;
    // The response body if the server answered 200, up to maxBytes
    virtual std::optional<std::string> get(const char* url, size_t maxBytes) = 0;
    // Makes a running get() (or the next one) return nothing soon; called from another thread
    virtual void cancel() = 0;
};

// Runs a due check on a background thread and records the result in the cache file
class UpdateChecker {
public:
    explicit UpdateChecker(std::unique_ptr<HttpFetcher> fetcher,
                           std::filesystem::path cachePath = UpdateCache::defaultPath());
    ~UpdateChecker(); // cancels a running check and waits for it

    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    // Starts a check if the cache says one is due and none is running; true if it started
    bool start(int64_t now);
    // True once after a check finished (whether it found anything or not): time to reload the cache
    bool poll() { return fFinished.exchange(false); }
    bool busy() const { return fBusy.load(); }
    // Stops a running check, if any, and waits for its thread
    void stop();

private:
    std::unique_ptr<HttpFetcher> fFetcher;
    std::filesystem::path fCachePath;
    std::thread fThread;
    std::atomic<bool> fBusy { false };
    std::atomic<bool> fFinished { false };
};

} // namespace xythos
