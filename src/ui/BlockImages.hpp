// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <string>
#include <vector>

namespace namsayin {

struct BuiltinImage
{
    const unsigned char* data;
    unsigned int size;
};

// The built-in image (WebP bytes, decodeWebp()) of a block type, or { nullptr, 0 } if it has none, and its small copy for the tiles and
// lists. BlockImages.cpp is generated at build time from resources/blocks/ by cmake/EmbedBlockImages.cmake.
BuiltinImage builtinBlockImage(const char* typeId);
BuiltinImage builtinBlockThumb(const char* typeId);
const std::vector<std::string>& builtinBlockIds(); // the typeIds that have one

// The built-in speaker cabinet images, which IR blocks show (FileLooks.hpp, defaultCabImage()), by image id: "<size>-<n>",
// e.g. "4x12-3". CabImages.cpp is generated the same way, from resources/cabs/ (tools/prepare_cab_images.py).
BuiltinImage builtinCabImage(const char* id);
BuiltinImage builtinCabThumb(const char* id);
const std::vector<std::string>& builtinCabIds();

} // namespace namsayin
