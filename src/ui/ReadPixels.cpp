// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "ReadPixels.hpp"

#include "OpenGL-include.hpp"

namespace namsayin {

void readFramebuffer(int x, int y, int width, int height, std::vector<uint8_t>& rgba)
{
    rgba.resize(static_cast<size_t>(width) * height * 4);
    if (rgba.empty())
        return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

} // namespace namsayin
