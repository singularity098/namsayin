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

TEST_CASE("the logo decodes, with its transparency")
{
    RgbaImage img;
    REQUIRE(decodeWebp(kLogoImage, kLogoImageSize, img));
    CHECK(img.width > 3 * img.height); // the crop circle beside the wordmark
    CHECK(img.height >= 256);          // sharp at the top bar's size at 3-4x scaling
    REQUIRE(img.pixels.size() == size_t(img.width) * img.height * 4);
    CHECK(img.pixels[3] == 0); // a corner is clear, so the bar shows through
}
