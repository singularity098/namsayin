// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

namespace namsayin {

struct BuiltinImage
{
    const unsigned char* data;
    unsigned int size;
};

// The built-in image (JPEG bytes) of a block type, or { nullptr, 0 } if it has none, and its small copy for the tiles and
// lists. BlockImages.cpp is generated at build time from resources/blocks/ by cmake/EmbedBlockImages.cmake.
BuiltinImage builtinBlockImage(const char* typeId);
BuiltinImage builtinBlockThumb(const char* typeId);

} // namespace namsayin
