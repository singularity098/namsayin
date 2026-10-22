// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

namespace namsayin {

struct BuiltinImage
{
    const unsigned char* data;
    unsigned int size;
};

// The built-in image (PNG bytes) of a block type, or { nullptr, 0 } if it has none. BlockImages.cpp is generated
// from resources/blocks/ by tools/embed_block_images.py.
BuiltinImage builtinBlockImage(const char* typeId);

} // namespace namsayin
