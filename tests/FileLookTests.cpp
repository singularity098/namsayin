// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "ui/FileLooks.hpp"

#include <cstdio>
#include <set>
#include <string>
#include <tuple>

using namespace namsayin;

namespace {

// Content hashes like the library's (XXH3-128 as 32 hex digits), different for each n
std::string fakeHash(int n)
{
    char text[40];
    std::snprintf(text, sizeof(text), "%08x%08x%08x%08x", n * 2654435761u, n ^ 0x5bd1e995u, n * 40503u, n + 7u);
    return text;
}

auto ampKey(const AmpLook& look) { return std::tuple(look.knobs, look.grille, look.badge, look.body); }
auto cabKey(const CabLook& look)
{
    return std::tuple(look.rows, look.columns, look.cone, look.cloth, look.badge, look.corners);
}

} // namespace

TEST_CASE("a file without a hash gets the generic look")
{
    const AmpLook amp = ampLook("");
    CHECK(amp.knobs == 6);
    CHECK(amp.grille == AmpLook::Slats);
    CHECK(amp.badge == BadgeNone);
    CHECK(amp.hue == 0.0f);
    const CabLook cab = cabLook("");
    CHECK(cab.rows == 2);
    CHECK(cab.columns == 2);
    CHECK(cab.badge == BadgeNone);
    CHECK(cab.hue == 0.0f);
}

TEST_CASE("a file always gets the same look")
{
    // fixed values: a change here changes how everyone's files look
    const AmpLook amp = ampLook("0123456789abcdef0123456789abcdef");
    CHECK(amp.knobs == 6);
    CHECK(amp.grille == AmpLook::Slats);
    CHECK(amp.badge == BadgeNone);
    CHECK(amp.body == AmpLook::Head);
    CHECK(amp.hue == doctest::Approx(18.6973f).epsilon(1e-4));
    const CabLook cab = cabLook("0123456789abcdef0123456789abcdef");
    CHECK(cab.rows == 2);
    CHECK(cab.columns == 2);
    CHECK(cab.cone == CabLook::Plain);
    CHECK(cab.cloth == CabLook::Bare);
    CHECK(cab.badge == BadgeCircle);
    CHECK_FALSE(cab.corners);
    CHECK(cab.hue == doctest::Approx(9.8193f).epsilon(1e-4));
    CHECK(cabKey(cabLook("0123456789abcdef0123456789abcdef")) == cabKey(cab));
}

TEST_CASE("looks stay in range and use every option")
{
    std::set<int> knobs, grilles, badges, bodies, layouts, cones, cloths, cabBadges, corners;
    for (int n = 0; n < 500; ++n)
    {
        const AmpLook amp = ampLook(fakeHash(n));
        CHECK(amp.knobs >= 3);
        CHECK(amp.knobs <= 7);
        CHECK(amp.hue >= -28.0f);
        CHECK(amp.hue <= 28.0f);
        for (const float turn : amp.turns)
        {
            CHECK(turn >= 0.0f);
            CHECK(turn <= 1.0f);
        }
        knobs.insert(amp.knobs);
        grilles.insert(amp.grille);
        badges.insert(amp.badge);
        bodies.insert(amp.body);

        const CabLook cab = cabLook(fakeHash(n));
        CHECK(cab.rows * cab.columns >= 1);
        CHECK(cab.rows * cab.columns <= 4);
        CHECK(cab.hue >= -60.0f);
        CHECK(cab.hue <= 60.0f);
        layouts.insert(cab.rows * 10 + cab.columns);
        cones.insert(cab.cone);
        cloths.insert(cab.cloth);
        cabBadges.insert(cab.badge);
        corners.insert(cab.corners);
    }
    CHECK(knobs.size() == 5);
    CHECK(grilles.size() == AmpLook::kGrilles);
    CHECK(badges.size() == kBadges);
    CHECK(bodies.size() == AmpLook::kBodies);
    CHECK(layouts.size() == 5);
    CHECK(cones.size() == CabLook::kCones);
    CHECK(cloths.size() == CabLook::kCloths);
    CHECK(cabBadges.size() == kBadges);
    CHECK(corners.size() == 2);
}

TEST_CASE("files in a library mostly look different")
{
    // 420 amp shapes and 840 cab shapes (before colour and knob positions): among 40 files few should share one
    std::set<decltype(ampKey(AmpLook()))> amps;
    std::set<decltype(cabKey(CabLook()))> cabs;
    for (int n = 0; n < 40; ++n)
    {
        amps.insert(ampKey(ampLook(fakeHash(n))));
        cabs.insert(cabKey(cabLook(fakeHash(n))));
    }
    CHECK(amps.size() >= 36);
    CHECK(cabs.size() >= 36);
}
