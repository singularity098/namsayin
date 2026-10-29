// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "ui/Blur.hpp"

#include <doctest/doctest.h>

#include <cstdlib>

using namespace namsayin;

namespace {

std::vector<uint8_t> image(int w, int h, uint8_t (*value)(int x, int y))
{
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t* px = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            px[0] = px[1] = px[2] = value(x, y);
            px[3] = 0;
        }
    return rgba;
}

int at(const BlurredImage& img, int x, int y) { return img.pixels[(static_cast<size_t>(y) * img.width + x) * 4]; }

} // namespace

TEST_CASE("blur: a flat image stays flat and comes out opaque at the shrunk size")
{
    const std::vector<uint8_t> rgba = image(37, 21, [](int, int) -> uint8_t { return 90; });
    BlurredImage out;
    blurDownsampled(rgba.data(), 37, 21, 4, 3, 3, false, out);
    CHECK(out.width == 10);
    CHECK(out.height == 6);
    for (size_t i = 0; i < out.pixels.size(); i += 4)
    {
        CHECK(out.pixels[i] == 90);
        CHECK(out.pixels[i + 3] == 255);
    }
}

TEST_CASE("blur: a sharp edge turns into a smooth ramp")
{
    // left half black, right half white: thin text-like detail is what the blur must wash out
    const std::vector<uint8_t> rgba = image(64, 8, [](int x, int) -> uint8_t { return x < 32 ? 0 : 255; });
    BlurredImage out;
    blurDownsampled(rgba.data(), 64, 8, 2, 3, 3, false, out);
    REQUIRE(out.width == 32);
    for (int x = 1; x < out.width; ++x)
        CHECK(at(out, x, 2) >= at(out, x - 1, 2)); // monotonic
    CHECK(at(out, 0, 2) == 0);
    CHECK(at(out, 31, 2) == 255);
    CHECK(std::abs(at(out, 15, 2) + at(out, 16, 2) - 255) <= 2); // symmetric around the edge
    CHECK(at(out, 14, 2) > 0);
    CHECK(at(out, 17, 2) < 255);
}

TEST_CASE("blur: bottom-up rows are flipped")
{
    // top row (in reading order) bright: with bottomUp the first row in memory is the bottom one
    const std::vector<uint8_t> rgba = image(4, 4, [](int, int y) -> uint8_t { return y == 3 ? 200 : 0; });
    BlurredImage out;
    blurDownsampled(rgba.data(), 4, 4, 1, 0, 0, true, out);
    CHECK(at(out, 0, 0) == 200);
    CHECK(at(out, 0, 3) == 0);
}

TEST_CASE("blur: an empty region gives an empty image")
{
    BlurredImage out;
    blurDownsampled(nullptr, 0, 0, 4, 3, 3, true, out);
    CHECK(out.width == 0);
    CHECK(out.pixels.empty());
}
