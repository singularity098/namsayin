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

TEST_CASE("the palettes offered, in order: the owner's first, then pale ones")
{
    const auto& palettes = presets();
    REQUIRE(palettes.size() == 5);
    CHECK(palettes[0].name == "Iris");
    CHECK(palettes[1].name == "Sunset");
    CHECK(palettes[2].name == "Ocean");
    CHECK(palettes[3].name == "Mint");
    CHECK(palettes[4].name == "Gold");
    CHECK(toHex(palettes[0].colors[0]) == "#7278fe");
    CHECK(toHex(palettes[0].colors[1]) == "#c688d6");
    CHECK(toHex(palettes[0].colors[2]) == "#a85eea");
    // the others are pale: light, and well short of full saturation
    for (size_t p = 1; p < palettes.size(); ++p)
        for (const Rgb color : palettes[p].colors)
        {
            CHECK(toHsv(color).v > 0.9);
            CHECK(toHsv(color).s < 0.45);
        }
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

TEST_CASE("ink settings round-trip and default to Splotchy in the owner's colours")
{
    Settings defaults;
    CHECK(defaults.inkMode == static_cast<int>(Mode::Splotch));
    CHECK(defaults.inkColors == std::vector<std::string> { "#7278fe", "#c688d6", "#a85eea" });
    CHECK(defaults.accentColor.empty());
    CHECK(defaults.savedColors.empty());

    Settings settings;
    settings.inkMode = 1;
    settings.inkColors = { "#00d2ff", "#3a7bd5" };
    settings.accentColor = "#b8bcff";
    settings.savedColors = { "#b8bcff", "#ffd2e0" };
    const Settings loaded = Settings::fromJson(settings.toJson());
    CHECK(loaded.inkMode == 1);
    CHECK(loaded.inkColors == std::vector<std::string> { "#00d2ff", "#3a7bd5" });
    CHECK(loaded.accentColor == "#b8bcff");
    CHECK(loaded.savedColors == std::vector<std::string> { "#b8bcff", "#ffd2e0" });

    // the keys of the unreleased first version are ignored, so everyone starts from the new default
    const Settings old = Settings::fromJson(R"({"inkMode": 0, "inkColors": ["#ff6a3d", "#ff2e88"]})");
    CHECK(old.inkMode == static_cast<int>(Mode::Splotch));
    CHECK(old.inkColors == defaults.inkColors);
}

TEST_CASE("HSV converts both ways")
{
    CHECK(fromHsv({ 0.0, 1.0, 1.0 }) == Rgb { 255, 0, 0 });
    CHECK(fromHsv({ 120.0, 1.0, 1.0 }) == Rgb { 0, 255, 0 });
    CHECK(fromHsv({ 240.0, 1.0, 1.0 }) == Rgb { 0, 0, 255 });
    CHECK(fromHsv({ 360.0, 1.0, 1.0 }) == Rgb { 255, 0, 0 });
    CHECK(fromHsv({ 77.0, 0.0, 0.5 }) == Rgb { 128, 128, 128 });
    const Hsv red = toHsv(Rgb { 255, 0, 0 });
    CHECK(red.h == doctest::Approx(0.0));
    CHECK(red.s == doctest::Approx(1.0));
    CHECK(red.v == doctest::Approx(1.0));
    CHECK(toHsv(Rgb { 0, 0, 0 }).s == 0.0);
    // every colour survives the round trip
    for (int r = 0; r < 256; r += 15)
        for (int g = 0; g < 256; g += 15)
            for (int b = 0; b < 256; b += 15)
            {
                const Rgb color { static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b) };
                CHECK(fromHsv(toHsv(color)) == color);
            }
}

TEST_CASE("colours get plain-English names")
{
    CHECK(colorName(Rgb { 0xb8, 0xbc, 0xff }) == "pale indigo");
    CHECK(colorName(Rgb { 0x72, 0x78, 0xfe }) == "light indigo");
    CHECK(colorName(Rgb { 0xff, 0x00, 0x00 }) == "vivid red");
    CHECK(colorName(Rgb { 0xff, 0xff, 0xff }) == "white");
    CHECK(colorName(Rgb { 0x00, 0x00, 0x00 }) == "black");
    CHECK(colorName(Rgb { 0x80, 0x80, 0x80 }) == "grey");
    CHECK(colorName(Rgb { 0x8b, 0x45, 0x13 }) == "brown");
    CHECK(colorName(Rgb { 0x80, 0x80, 0x00 }) == "olive");
    CHECK(colorName(Rgb { 0xff, 0xc8, 0xa8 }) == "pale peach");
    CHECK(colorName(Rgb { 0x00, 0x60, 0x50 }) == "deep teal");
    CHECK(colorName(Rgb { 0x08, 0x10, 0x40 }) == "dark blue");
    // every colour gets a name
    for (int h = 0; h < 360; h += 5)
        for (const double s : { 0.05, 0.3, 0.5, 0.9 })
            for (const double v : { 0.1, 0.5, 0.9 })
                CHECK_FALSE(colorName(fromHsv({ static_cast<double>(h), s, v })).empty());
}
