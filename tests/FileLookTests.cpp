// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "ui/FileLooks.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace namsayin;

namespace {

// File paths like a library's, different for each n
std::string fakePath(int n) { return "/home/me/Guitar/Models/Amp " + std::to_string(n) + ".nam"; }

auto ampKey(const AmpLook& look) { return std::tuple(look.knobs, look.grille, look.badge); }
auto cabKey(const CabLook& look)
{
    return std::tuple(look.cone, look.cloth, look.badge, look.corners);
}

} // namespace

TEST_CASE("no file gets the generic look")
{
    const AmpLook amp = ampLook("");
    CHECK(amp.knobs == 6);
    CHECK(amp.grille == AmpLook::Slats);
    CHECK(amp.badge == BadgeNone);
    CHECK(amp.hue == 0.0f);
    const CabLook cab = cabLook("");
    CHECK(cab.cone == CabLook::Plain);
    CHECK(cab.badge == BadgeNone);
    CHECK(cab.hue == 0.0f);
}

TEST_CASE("a file always gets the same look")
{
    // fixed values: a change here changes how everyone's files look
    const char* path = "/home/me/Guitar/Models/Plexi.nam";
    const AmpLook amp = ampLook(path);
    CHECK(amp.knobs == 5);
    CHECK(amp.grille == AmpLook::Crosshatch);
    CHECK(amp.badge == BadgeStar);
    CHECK(amp.hue == doctest::Approx(0.99096f).epsilon(1e-4));
    const CabLook cab = cabLook(path);
    CHECK(cab.cone == CabLook::BigCap);
    CHECK(cab.cloth == CabLook::Vertical);
    CHECK(cab.badge == BadgeStar);
    CHECK_FALSE(cab.corners);
    CHECK(cab.hue == doctest::Approx(-8.5296f).epsilon(1e-4));
    CHECK(cabKey(cabLook(path)) == cabKey(cab));
}

TEST_CASE("looks stay in range and use every option")
{
    std::set<int> knobs, grilles, badges, cones, cloths, cabBadges, corners;
    int reds = 0, oranges = 0;
    for (int n = 0; n < 500; ++n)
    {
        const AmpLook amp = ampLook(fakePath(n));
        CHECK(amp.knobs >= 3);
        CHECK(amp.knobs <= 7);
        // reds and oranges, mostly red
        CHECK(amp.hue >= AmpLook::kMinHue);
        CHECK(amp.hue <= AmpLook::kMaxHue);
        reds += amp.hue < 15.0f ? 1 : 0;
        oranges += amp.hue >= 22.0f ? 1 : 0;
        for (const float turn : amp.turns)
        {
            CHECK(turn >= 0.0f);
            CHECK(turn <= 1.0f);
        }
        knobs.insert(amp.knobs);
        grilles.insert(amp.grille);
        badges.insert(amp.badge);

        const CabLook cab = cabLook(fakePath(n));
        CHECK(cab.hue >= -60.0f);
        CHECK(cab.hue <= 60.0f);
        cones.insert(cab.cone);
        cloths.insert(cab.cloth);
        cabBadges.insert(cab.badge);
        corners.insert(cab.corners);
    }
    CHECK(knobs.size() == 5);
    CHECK(grilles.size() == AmpLook::kGrilles);
    CHECK(badges.size() == kBadges);
    CHECK(reds > 300);
    CHECK(oranges > 50);
    CHECK(cones.size() == CabLook::kCones);
    CHECK(cloths.size() == CabLook::kCloths);
    CHECK(cabBadges.size() == kBadges);
    CHECK(corners.size() == 2);
}

TEST_CASE("files in a library mostly look different")
{
    // 210 amp shapes and 168 cab shapes (before colour and knob positions): among 40 files few should share one
    std::set<decltype(ampKey(AmpLook()))> amps;
    std::set<decltype(cabKey(CabLook()))> cabs;
    for (int n = 0; n < 40; ++n)
    {
        amps.insert(ampKey(ampLook(fakePath(n))));
        cabs.insert(cabKey(cabLook(fakePath(n))));
    }
    CHECK(amps.size() >= 36);
    CHECK(cabs.size() >= 32);
}

namespace {

// The built-in cab images' ids, as resources/cabs/ has them
std::vector<std::string> cabIds()
{
    std::vector<std::string> ids;
    for (const auto& entry : std::filesystem::directory_iterator(NAMSAYIN_CAB_IMAGES))
        if (entry.path().extension() == ".webp")
            ids.push_back(entry.path().stem().string());
    return ids;
}

} // namespace

TEST_CASE("cab image ids: sizes, and in their numbers' order")
{
    CHECK(cabImageSize("4x12-3") == "4x12");
    CHECK(cabImageSize("1x10-12") == "1x10");
    CHECK(cabImageSize("nothing") == "");
    const std::vector<std::string> ids { "4x12-10", "2x12-1", "4x12-2", "4x12-1" };
    CHECK(cabImagesOfSize(ids, "4x12") == std::vector<std::string> { "4x12-1", "4x12-2", "4x12-10" });
    CHECK(cabImagesOfSize(ids, "1x10").empty());

    // every size has images, and every image a size
    const std::vector<std::string> all = cabIds();
    size_t count = 0;
    for (const char* size : kCabSizes)
    {
        CAPTURE(size);
        const std::vector<std::string> sized = cabImagesOfSize(all, size);
        CHECK_FALSE(sized.empty());
        for (size_t i = 0; i < sized.size(); ++i)
            CHECK(sized[i] == std::string(size) + "-" + std::to_string(i + 1)); // numbered 1, 2, 3...
        count += sized.size();
    }
    CHECK(count == all.size());
}

TEST_CASE("the cab size in a file's name")
{
    CHECK(cabSizeInName("Mesa 4x12 V30 SM57") == "4x12");
    CHECK(cabSizeInName("ampeg_8X10") == "");   // not a size we have images of
    CHECK(cabSizeInName("Ampeg 4X10 bass") == "4x10");
    CHECK(cabSizeInName("1x12-Blue") == "1x12");
    CHECK(cabSizeInName("V30 112") == "");
    CHECK(cabSizeInName("14x12") == "");        // part of a longer number
    CHECK(cabSizeInName("2x120") == "");
    CHECK(cabSizeInName("") == "");
}

TEST_CASE("each IR gets a cab image from its hash, of the size its name gives")
{
    const std::vector<std::string> ids = cabIds();
    REQUIRE_FALSE(ids.empty());
    CHECK(defaultCabImage("", "", {}).empty());
    CHECK(defaultCabImage("", "", ids) == "4x12-1"); // no file

    // the same file always gets the same cab
    const std::string hash = "00112233445566778899aabbccddeeff";
    CHECK(defaultCabImage(hash, "Room", ids) == defaultCabImage(hash, "Room", ids));

    // spread over every image; a size in the name keeps it to that size
    std::map<std::string, int> uses;
    for (int i = 0; i < 4000; ++i)
    {
        const std::string key = "hash " + std::to_string(i);
        ++uses[defaultCabImage(key, "Some IR", ids)];
        CHECK(cabImageSize(defaultCabImage(key, "Some 2x10 IR", ids)) == "2x10");
    }
    CHECK(uses.size() == ids.size());
    for (const auto& [id, n] : uses)
        CHECK(n > 4000 / static_cast<int>(ids.size()) / 3);
}
