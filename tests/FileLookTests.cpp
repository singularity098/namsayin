// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "ui/FileLooks.hpp"

#include <set>
#include <string>
#include <tuple>

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
