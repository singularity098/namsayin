// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "Blur.hpp"

#include <algorithm>

namespace namsayin {

namespace {

// One box blur pass along a row or column of `count` pixels, `stride` apart (in pixels), 3 channels
void boxPass(uint8_t* data, int count, int stride, int radius, std::vector<uint32_t>& line)
{
    line.resize(static_cast<size_t>(count) * 3);
    for (int i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c)
            line[i * 3 + c] = data[static_cast<size_t>(i) * stride * 4 + c];

    const uint32_t window = 2 * radius + 1;
    uint32_t sum[3] = {};
    const auto at = [&](int i, int c) { return line[std::clamp(i, 0, count - 1) * 3 + c]; };
    for (int i = -radius; i <= radius; ++i)
        for (int c = 0; c < 3; ++c)
            sum[c] += at(i, c);
    for (int i = 0; i < count; ++i)
    {
        uint8_t* px = data + static_cast<size_t>(i) * stride * 4;
        for (int c = 0; c < 3; ++c)
        {
            px[c] = static_cast<uint8_t>((sum[c] + window / 2) / window);
            sum[c] += at(i + radius + 1, c) - at(i - radius, c);
        }
    }
}

} // namespace

void blurDownsampled(const uint8_t* rgba, int width, int height, int factor, int radius, int passes, bool bottomUp,
                     BlurredImage& out)
{
    factor = std::max(factor, 1);
    radius = std::max(radius, 0);
    out.width = width > 0 ? (width + factor - 1) / factor : 0;
    out.height = height > 0 ? (height + factor - 1) / factor : 0;
    out.pixels.assign(static_cast<size_t>(out.width) * out.height * 4, 255);
    if (out.width == 0 || out.height == 0)
        return;

    for (int oy = 0; oy < out.height; ++oy)
    {
        const int y0 = oy * factor, y1 = std::min(y0 + factor, height);
        for (int ox = 0; ox < out.width; ++ox)
        {
            const int x0 = ox * factor, x1 = std::min(x0 + factor, width);
            uint32_t sum[3] = {};
            for (int y = y0; y < y1; ++y)
            {
                const int row = bottomUp ? height - 1 - y : y;
                const uint8_t* px = rgba + (static_cast<size_t>(row) * width + x0) * 4;
                for (int x = x0; x < x1; ++x, px += 4)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += px[c];
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            uint8_t* dst = &out.pixels[(static_cast<size_t>(oy) * out.width + ox) * 4];
            for (int c = 0; c < 3; ++c)
                dst[c] = static_cast<uint8_t>((sum[c] + n / 2) / n);
        }
    }

    if (radius == 0)
        return;
    std::vector<uint32_t> line;
    for (int pass = 0; pass < passes; ++pass)
    {
        for (int y = 0; y < out.height; ++y)
            boxPass(&out.pixels[static_cast<size_t>(y) * out.width * 4], out.width, 1, radius, line);
        for (int x = 0; x < out.width; ++x)
            boxPass(&out.pixels[static_cast<size_t>(x) * 4], out.height, out.width, radius, line);
    }
}

} // namespace namsayin
