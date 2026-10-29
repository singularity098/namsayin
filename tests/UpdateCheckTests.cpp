// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "state/UpdateCheck.hpp"
#include "ui/HttpGet.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>

using namespace namsayin;

namespace {

// Answers with a fixed body; or, with block set, waits until cancelled
class FakeFetcher final : public HttpFetcher {
public:
    FakeFetcher(std::optional<std::string> body, bool block = false)
        : fBody(std::move(body))
        , fBlock(block)
    {
    }

    std::optional<std::string> get(const char* url, size_t) override
    {
        std::unique_lock lock(fMutex);
        fUrl = url;
        ++fCalls;
        if (fBlock)
        {
            fCv.wait(lock, [this] { return fCancelled; });
            return std::nullopt;
        }
        return fBody;
    }

    void cancel() override
    {
        std::lock_guard lock(fMutex);
        fCancelled = true;
        fCv.notify_all();
    }

    std::mutex fMutex;
    std::condition_variable fCv;
    std::optional<std::string> fBody;
    bool fBlock = false;
    bool fCancelled = false;
    std::string fUrl;
    int fCalls = 0;
};

std::filesystem::path testCachePath()
{
    const auto dir = std::filesystem::temp_directory_path() / "namsayin-test-update";
    std::filesystem::remove_all(dir);
    return dir / "update-check.json";
}

void waitFor(UpdateChecker& checker)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (checker.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
}

} // namespace

TEST_CASE("versions parse and compare")
{
    CHECK(parseVersion("1.2.3") == Version { 1, 2, 3 });
    CHECK(parseVersion("v0.10.0") == Version { 0, 10, 0 });
    CHECK(parseVersion("V2") == Version { 2, 0, 0 });
    CHECK(parseVersion("1.2") == Version { 1, 2, 0 });
    for (const char* bad : { "", "v", "1.", ".1", "1.2.3.4", "1.2.3-beta", "1.2a", "latest", "1..2", "-1",
                             "12345678" })
        CHECK_MESSAGE(!parseVersion(bad), bad);

    CHECK(isNewerVersion("0.2.0", "0.1.0"));
    CHECK(isNewerVersion("v1.0", "0.9.9"));
    CHECK(isNewerVersion("0.1.10", "0.1.9")); // numbers, not text
    CHECK(!isNewerVersion("0.1.0", "0.1.0"));
    CHECK(!isNewerVersion("0.1.0", "0.2.0"));
    CHECK(!isNewerVersion("garbage", "0.1.0"));
    CHECK(!isNewerVersion("", "0.1.0"));
    CHECK(parseVersion(kAppVersion)); // the build's own version is one
}

TEST_CASE("the release version comes from the tag, never from drafts or pre-releases")
{
    CHECK(releaseVersion(R"({"tag_name": "v1.2.0", "draft": false, "prerelease": false})") == "1.2.0");
    CHECK(releaseVersion(R"({"tag_name": "1.3"})") == "1.3.0");
    CHECK(!releaseVersion(R"({"tag_name": "v1.2.0", "prerelease": true})"));
    CHECK(!releaseVersion(R"({"tag_name": "v1.2.0", "draft": true})"));
    CHECK(!releaseVersion(R"({"tag_name": "v1.2.0-rc1"})"));
    CHECK(!releaseVersion(R"({"tag_name": 12})"));
    CHECK(!releaseVersion(R"({"message": "Not Found"})"));
    CHECK(!releaseVersion("[1, 2]"));
    CHECK(!releaseVersion("<html>"));
    CHECK(!releaseVersion(""));
}

TEST_CASE("the update cache round-trips and is due once a day")
{
    const UpdateCache cache { 1700000000, "1.4.2" };
    CHECK(UpdateCache::fromJson(cache.toJson()) == cache);
    CHECK(UpdateCache::fromJson("nonsense") == UpdateCache {});
    CHECK(UpdateCache::fromJson(R"({"checkedAt": "x", "latest": "<script>"})") == UpdateCache {});

    const auto path = testCachePath();
    CHECK(UpdateCache::load(path) == UpdateCache {});
    REQUIRE(cache.save(path));
    CHECK(UpdateCache::load(path) == cache);

    CHECK(UpdateCache {}.checkDue(1700000000));
    CHECK(!cache.checkDue(cache.checkedAt + 60));
    CHECK(!cache.checkDue(cache.checkedAt + UpdateCache::kInterval - 1));
    CHECK(cache.checkDue(cache.checkedAt + UpdateCache::kInterval));
    CHECK(cache.checkDue(cache.checkedAt - 60)); // the clock went back
}

TEST_CASE("the banner offers newer versions that weren't dismissed")
{
    const UpdateCache cache { 1, "0.3.0" };
    CHECK(updateToShow(cache, "0.2.0", "") == "0.3.0");
    CHECK(!updateToShow(cache, "0.3.0", ""));    // up to date
    CHECK(!updateToShow(cache, "0.4.0", ""));    // newer than the latest release (a local build)
    CHECK(!updateToShow(cache, "0.2.0", "0.3.0")); // dismissed
    CHECK(!updateToShow(cache, "0.2.0", "0.5.0"));
    CHECK(updateToShow(cache, "0.2.0", "0.2.5") == "0.3.0"); // a newer one than the dismissed one
    CHECK(updateToShow(cache, "0.2.0", "junk") == "0.3.0");
    CHECK(!updateToShow(UpdateCache {}, "0.2.0", ""));
}

TEST_CASE("the update checker checks when due and records the latest release")
{
    const auto path = testCachePath();
    auto fake = std::make_unique<FakeFetcher>(R"({"tag_name": "v9.8.7"})");
    FakeFetcher* fetcher = fake.get();
    UpdateChecker checker(std::move(fake), path);

    const int64_t now = 1800000000;
    REQUIRE(checker.start(now));
    waitFor(checker);
    CHECK(checker.poll());
    CHECK(!checker.poll()); // once
    CHECK(fetcher->fUrl == kLatestReleaseApiUrl);
    CHECK(UpdateCache::load(path) == UpdateCache { now, "9.8.7" });

    // not again within a day, and not by another instance either
    CHECK(!checker.start(now + 3600));
    UpdateChecker other(std::make_unique<FakeFetcher>(std::nullopt), path);
    CHECK(!other.start(now + 3600));
    CHECK(fetcher->fCalls == 1);

    // a day later it checks again; a failed check keeps the last known release
    fetcher->fBody = std::nullopt;
    REQUIRE(checker.start(now + UpdateCache::kInterval));
    waitFor(checker);
    CHECK(checker.poll());
    CHECK(UpdateCache::load(path) == UpdateCache { now + UpdateCache::kInterval, "9.8.7" });
}

TEST_CASE("the update checker's destructor cancels a running check")
{
    const auto path = testCachePath();
    {
        UpdateChecker checker(std::make_unique<FakeFetcher>(std::nullopt, true), path);
        REQUIRE(checker.start(1800000000));
        CHECK(checker.busy());
    } // would hang here if the check weren't cancelled
    CHECK(UpdateCache::load(path).latest.empty());
}

TEST_CASE("HttpGet gives nothing for unreachable servers, non-HTTPS URLs and after cancel")
{
    HttpGet http;
    CHECK(!http.get("https://127.0.0.1:1/", 1024)); // nothing listens on port 1
    CHECK(!http.get("http://127.0.0.1:1/", 1024));
    http.cancel();
    CHECK(!http.get("https://127.0.0.1:1/", 1024));
}
