// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "ui/BackgroundImage.hpp"
#include "ui/LogoImage.hpp"
#include "ui/WebpImage.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace xythos;

namespace {

std::vector<unsigned char> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

} // namespace

TEST_CASE("every built-in block image and thumbnail decodes, 1:2 at its size")
{
    const std::filesystem::path images = XYTHOS_BLOCK_IMAGES;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(images))
    {
        if (entry.path().extension() != ".webp")
            continue;
        CAPTURE(entry.path().filename().string());
        ++count;
        for (const auto& [path, height] : { std::pair { entry.path(), 1280u },
                                            std::pair { images / "thumbs" / entry.path().filename(), 384u } })
        {
            const std::vector<unsigned char> data = readFile(path);
            RgbaImage img;
            REQUIRE(decodeWebp(data.data(), data.size(), img));
            CHECK(img.height == height);
            CHECK(img.width == height / 2);
            CHECK(img.pixels.size() == static_cast<size_t>(img.width) * img.height * 4);
        }
    }
    CHECK(count > 80);
}

TEST_CASE("every built-in cab image and thumbnail decodes, at its size")
{
    const std::filesystem::path images = XYTHOS_CAB_IMAGES;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(images))
    {
        if (entry.path().extension() != ".webp")
            continue;
        CAPTURE(entry.path().filename().string());
        ++count;
        for (const auto& [path, longest] : { std::pair { entry.path(), 896u },
                                             std::pair { images / "thumbs" / entry.path().filename(), 320u } })
        {
            const std::vector<unsigned char> data = readFile(path);
            RgbaImage img;
            REQUIRE(decodeWebp(data.data(), data.size(), img));
            CHECK(std::max(img.width, img.height) == longest);
            CHECK(std::min(img.width, img.height) > longest / 2);
        }
    }
    CHECK(count >= 42);
}

TEST_CASE("every built-in generic pedal image and thumbnail decodes, at its height")
{
    const std::filesystem::path images = XYTHOS_PEDAL_IMAGES;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(images))
    {
        if (entry.path().extension() != ".webp")
            continue;
        CAPTURE(entry.path().filename().string());
        ++count;
        for (const auto& [path, height] : { std::pair { entry.path(), 1280u },
                                            std::pair { images / "thumbs" / entry.path().filename(), 384u } })
        {
            const std::vector<unsigned char> data = readFile(path);
            RgbaImage img;
            REQUIRE(decodeWebp(data.data(), data.size(), img));
            CHECK(img.height == height);
            CHECK(img.width > height / 3);
            CHECK(img.width < height);
        }
    }
    CHECK(count >= 49);
}

TEST_CASE("the background image decodes, and other data isn't taken for a WebP")
{
    RgbaImage img;
    REQUIRE(decodeWebp(kBackgroundImage, kBackgroundImageSize, img));
    CHECK(img.width > 1000);
    CHECK(img.height > 500);

    const unsigned char png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13 };
    CHECK_FALSE(isWebp(png, sizeof(png)));
    CHECK_FALSE(decodeWebp(png, sizeof(png), img));
    // a damaged WebP: the header, nothing after it
    CHECK_FALSE(decodeWebp(kBackgroundImage, 30, img));
    CHECK(img.pixels.empty());
}

TEST_CASE("the logo's layers decode, the same size, with their transparency")
{
    RgbaImage mark, wordmark;
    REQUIRE(decodeWebp(kLogoMark, kLogoMarkSize, mark));
    REQUIRE(decodeWebp(kLogoWordmark, kLogoWordmarkSize, wordmark));
    // drawn over each other, so they line up
    CHECK(mark.width == wordmark.width);
    CHECK(mark.height == wordmark.height);
    CHECK(mark.width > 3 * mark.height); // the crop circle beside the wordmark
    CHECK(mark.height >= 256);           // sharp at the top bar's size at 3-4x scaling
    REQUIRE(mark.pixels.size() == size_t(mark.width) * mark.height * 4);
    CHECK(mark.pixels[3] == 0); // a corner is clear, so the bar shows through
    CHECK(wordmark.pixels[3] == 0);
    // the crop circle is white, so tinting it gives the highlight colour; it has no letters, the wordmark no circle
    const auto at = [](const RgbaImage& img, int x, int y) { return &img.pixels[(size_t(y) * img.width + x) * 4]; };
    const int cx = mark.height * 76 / 152, cy = mark.height * 76 / 152; // the centre dot (76, 76 into the viewBox)
    CHECK(at(mark, cx, cy)[3] > 200);
    CHECK(at(mark, cx, cy)[0] > 240);
    CHECK(at(mark, cx, cy)[2] > 240);
    CHECK(at(wordmark, cx, cy)[3] == 0);
}
