// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "state/Ink.hpp"
#include "state/Settings.hpp"


using namespace xythos;
using namespace xythos::ink;

TEST_CASE("every preset colour is light enough to read on the window")
{
    for (const Palette& palette : presets())
    {
        CHECK(palette.colors.size() >= 2);
        CHECK(palette.colors.size() <= 3);
        for (const Rgb color : palette.colors)
            CHECK(isLightEnough(color));
    }
}

TEST_CASE("the demo palettes are the ones offered, in order")
{
    const auto& palettes = presets();
    REQUIRE(palettes.size() == 4);
    CHECK(palettes[0].name == "Sunset");
    CHECK(palettes[1].name == "Ocean");
    CHECK(palettes[2].name == "Mint");
    CHECK(palettes[3].name == "Gold");
    CHECK(toHex(palettes[0].colors[0]) == "#ff6a3d");
    CHECK(toHex(palettes[3].colors[2]) == "#ff3d6e");
}

TEST_CASE("dark colours are lifted toward white, light ones are kept")
{
    CHECK(isLightEnough(Rgb { 0xff, 0xff, 0xff }));
    CHECK_FALSE(isLightEnough(Rgb { 0x00, 0x00, 0x00 }));
    CHECK_FALSE(isLightEnough(Rgb { 0x20, 0x10, 0x60 }));

    const Rgb light { 0x7b, 0x5c, 0xff };
    CHECK(lighten(light) == light);

    for (const Rgb dark : { Rgb { 0, 0, 0 }, Rgb { 0x20, 0x10, 0x60 }, Rgb { 0, 0, 0x30 }, Rgb { 0x10, 0x40, 0 } })
    {
        const Rgb lifted = lighten(dark);
        CHECK(isLightEnough(lifted));
        // Lifted toward white: no channel goes down
        CHECK(lifted.r >= dark.r);
        CHECK(lifted.g >= dark.g);
        CHECK(lifted.b >= dark.b);
    }
}

TEST_CASE("hex colours parse and format")
{
    CHECK(toHex(Rgb { 0xff, 0x6a, 0x3d }) == "#ff6a3d");
    CHECK(parseHex("#ff6a3d") == Rgb { 0xff, 0x6a, 0x3d });
    CHECK(parseHex("FF6A3D") == Rgb { 0xff, 0x6a, 0x3d });
    CHECK_FALSE(parseHex("#ff6a3").has_value());
    CHECK_FALSE(parseHex("#ff6a3g").has_value());
    CHECK_FALSE(parseHex("").has_value());
}

TEST_CASE("sampling interpolates evenly spaced colours")
{
    const std::vector<Rgb> colors { { 0, 0, 0 }, { 255, 0, 0 }, { 255, 255, 255 } };
    CHECK(sample(colors, 0.0) == Rgb { 0, 0, 0 });
    CHECK(sample(colors, 0.5) == Rgb { 255, 0, 0 });
    CHECK(sample(colors, 1.0) == Rgb { 255, 255, 255 });
    CHECK(sample(colors, 0.25).r == 128);
    CHECK(sample(colors, 2.0) == Rgb { 255, 255, 255 });
    CHECK(sample({ { 9, 8, 7 } }, 0.7) == Rgb { 9, 8, 7 });
}

TEST_CASE("the smooth texture runs from the first colour to the last")
{
    const std::vector<Rgb> colors { { 0xff, 0x6a, 0x3d }, { 0xff, 0x2e, 0x88 }, { 0x7b, 0x5c, 0xff } };
    const auto pixels = makeTexture(Mode::Smooth, colors);
    REQUIRE(pixels.size() == static_cast<size_t>(kTextureWidth * kTextureHeight * 4));
    // Top-left is the first colour, the bottom-right corner the last (the tilt puts the top-right at about 0.9)
    const auto left = pixels.begin();
    const auto right = pixels.begin() + ((kTextureHeight - 1) * kTextureWidth + kTextureWidth - 1) * 4;
    CHECK(left[0] == 0xff);
    CHECK(left[1] < 0x90);
    CHECK(right[2] >= 0xf0); // t is about 0.996 at the very corner
    for (size_t i = 3; i < pixels.size(); i += 4)
        CHECK(pixels[i] == 255);
}

TEST_CASE("the splotch texture uses every colour and is the same every time")
{
    const std::vector<Rgb> colors { { 0xff, 0x6a, 0x3d }, { 0x00, 0xd2, 0xff }, { 0x6f, 0x5c, 0xff } };
    const auto first = makeTexture(Mode::Splotch, colors);
    const auto second = makeTexture(Mode::Splotch, colors);
    CHECK(first == second);

    bool hasFirst = false, hasLast = false;
    for (size_t i = 0; i < first.size(); i += 4)
    {
        const Rgb pixel { first[i], first[i + 1], first[i + 2] };
        hasFirst = hasFirst || pixel == colors.front();
        hasLast = hasLast || pixel == colors.back();
    }
    CHECK(hasFirst);
    CHECK(hasLast);
}

TEST_CASE("ink settings round-trip and default to the accent with Sunset")
{
    Settings defaults;
    CHECK(defaults.inkMode == 0);
    CHECK(defaults.inkColors == std::vector<std::string> { "#ff6a3d", "#ff2e88", "#7b5cff" });

    Settings settings;
    settings.inkMode = 2;
    settings.inkColors = { "#00d2ff", "#3a7bd5" };
    const Settings loaded = Settings::fromJson(settings.toJson());
    CHECK(loaded.inkMode == 2);
    CHECK(loaded.inkColors == std::vector<std::string> { "#00d2ff", "#3a7bd5" });
}
