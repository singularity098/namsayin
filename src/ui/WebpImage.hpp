// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace namsayin {

// A decoded image: 8-bit RGBA, rows top to bottom
struct RgbaImage {
    unsigned width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

// Whether the bytes are a WebP file (a RIFF container of type WEBP)
bool isWebp(const unsigned char* data, size_t size);

// Decodes a WebP file (the built-in images: DPF's stb_image reads only JPEG and PNG). Plain C++, no DPF, so the tests
// can check every embedded image decodes. False if it isn't one or is damaged.
bool decodeWebp(const unsigned char* data, size_t size, RgbaImage& out);

} // namespace namsayin
