// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xythos::ink {

// How the highlight colour and the text are painted: one colour (the accent, Settings::accent), or 2-3 colours
// spread across the window
enum class Mode : int { Solid = 0, Smooth = 1, Splotch = 2 };

struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;

    bool operator==(const Rgb&) const = default;
};

// A preset: its name in Settings, and its colours (2 or 3)
struct Palette
{
    std::string_view name;
    std::vector<Rgb> colors;
};

// The palettes Settings offers, in the order they're shown. Each is light enough to read on the dark window.
const std::vector<Palette>& presets();

// The smallest relative luminance a colour may have (WCAG-style, 0..1): about 3:1 against the window's darkest
// background, so every colour reads as light. Colours below it are lifted toward white (lighten()).
constexpr double kMinLuminance = 0.12;

double relativeLuminance(Rgb color);
bool isLightEnough(Rgb color);
// The colour, or the nearest one toward white that is light enough
Rgb lighten(Rgb color);

// "#rrggbb" (the leading # is optional in the input)
std::string toHex(Rgb color);
std::optional<Rgb> parseHex(std::string_view text);

// The texture the paint is drawn from: kTextureWidth x kTextureHeight RGBA pixels, stretched over the window.
// Smooth runs the colours left to right, slightly tilted; Splotch fills the window with soft, almost random blobs
// of them (fixed noise, so it looks the same every time). Solid is one colour.
constexpr int kTextureWidth = 128;
constexpr int kTextureHeight = 80;
std::vector<uint8_t> makeTexture(Mode mode, const std::vector<Rgb>& colors);

// The colour at t (0..1) along the colours, evenly spaced
Rgb sample(const std::vector<Rgb>& colors, double t);

} // namespace xythos::ink
