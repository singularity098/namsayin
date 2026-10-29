// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>
#include <vector>

namespace namsayin {

// A blurred copy of part of the window, drawn under the popups so what's behind them reads as a soft blur
// (owner, 2026-10-29). RGBA, 8 bits per channel, rows top to bottom, opaque.
struct BlurredImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// Shrinks `rgba` (width x height, RGBA) by `factor` in each direction (averaging each factor x factor block),
// then box-blurs it `passes` times with the given radius (in the shrunk image's pixels; three passes are close to a
// Gaussian), clamping at the edges. `bottomUp`: the rows come bottom row first, as glReadPixels gives them.
// Reuses `out`'s memory.
void blurDownsampled(const uint8_t* rgba, int width, int height, int factor, int radius, int passes, bool bottomUp,
                     BlurredImage& out);

} // namespace namsayin
