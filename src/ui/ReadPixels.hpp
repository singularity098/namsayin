// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>
#include <vector>

namespace namsayin {

// Reads a rectangle of the current OpenGL framebuffer (x, y from its bottom left, in device pixels) as RGBA, rows
// bottom first. Kept out of the DPF code so the GL headers stay here, like Vsync.cpp.
void readFramebuffer(int x, int y, int width, int height, std::vector<uint8_t>& rgba);

} // namespace namsayin
