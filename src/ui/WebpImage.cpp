// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "WebpImage.hpp"

#include "webp/decode.h"

#include <cstring>

namespace namsayin {

bool isWebp(const unsigned char* data, size_t size)
{
    return data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0;
}

bool decodeWebp(const unsigned char* data, size_t size, RgbaImage& out)
{
    int width = 0, height = 0;
    if (!isWebp(data, size) || !WebPGetInfo(data, size, &width, &height) || width <= 0 || height <= 0)
        return false;
    out.width = static_cast<unsigned>(width);
    out.height = static_cast<unsigned>(height);
    out.pixels.resize(static_cast<size_t>(width) * height * 4);
    const int stride = width * 4;
    if (WebPDecodeRGBAInto(data, size, out.pixels.data(), out.pixels.size(), stride) == nullptr)
    {
        out = {};
        return false;
    }
    return true;
}

} // namespace namsayin
