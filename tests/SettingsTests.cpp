// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "state/Paths.hpp"
#include "state/Settings.hpp"

#include <algorithm>
#include <filesystem>

using namespace namsayin;

TEST_CASE("settings round-trip through JSON and a file")
{
    Settings settings;
    settings.namDir = "/home/me/NAM models/ünïcode";
    settings.irDir = "C:\\IRs";
    settings.a2Lite = true;
    settings.normalize = false;
    settings.parallel = false;
    settings.accent = 3;
    settings.favorites = { "00112233445566778899aabbccddeeff", "ffeeddccbbaa99887766554433221100" };
    settings.embedFiles = false;
    settings.recentPresets = { "/a.namsayin", "/b.namsayin" };
    settings.defaultPreset = "/presets/Clean/Twin.namsayin";

    CHECK(Settings::fromJson(settings.toJson()) == settings);

    const auto path = std::filesystem::temp_directory_path() / "namsayin-test-settings" / "settings.json";
    std::filesystem::remove_all(path.parent_path());
    REQUIRE(settings.save(path));
    CHECK(Settings::load(path) == settings);
}

TEST_CASE("settings fall back to defaults for missing or broken values")
{
    CHECK(Settings::fromJson("") == Settings {});
    CHECK(Settings::fromJson("not json") == Settings {});
    CHECK(Settings::fromJson("[1, 2]") == Settings {});
    CHECK(Settings::load(std::filesystem::temp_directory_path() / "namsayin-no-such-settings.json") == Settings {});

    const Settings partial = Settings::fromJson(R"({"namDir": "/nam", "accent": "purple", "a2Lite": true})");
    CHECK(partial.namDir == "/nam");
    CHECK(partial.a2Lite);
    CHECK(partial.accent == 7); // wrong type: default (white)
    CHECK(partial.normalize);
}

TEST_CASE("paths convert to and from UTF-8")
{
    const std::string text = "/tmp/ämp — 1.nam";
    CHECK(pathToUtf8(pathFromUtf8(text)) == text);
}

TEST_CASE("recent presets: most recent first, no duplicates, bounded")
{
    Settings settings;
    for (int i = 0; i < 12; ++i)
        settings.addRecentPreset("/p" + std::to_string(i));
    settings.addRecentPreset("/p5");
    REQUIRE(settings.recentPresets.size() == Settings::kMaxRecentPresets);
    CHECK(settings.recentPresets[0] == "/p5");
    CHECK(settings.recentPresets[1] == "/p11");
    CHECK(std::count(settings.recentPresets.begin(), settings.recentPresets.end(), "/p5") == 1);
}
