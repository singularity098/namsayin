// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "ui/BackgroundImage.hpp"
#include "ui/WebpImage.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace namsayin;

namespace {

std::vector<unsigned char> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

} // namespace

TEST_CASE("every built-in block image and thumbnail decodes, 1:2 at its size")
{
    const std::filesystem::path images = NAMSAYIN_BLOCK_IMAGES;
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
