// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

#include <utility>
#include <vector>

namespace namsayin {

// The symbols on the face of the pedals in the placeholder art: each block type has its own, drawn from its name and
// what it does. Plain data, no DPF, so the tests can check that every pedal has one and no two look the same.
//
// Coordinates are in the pedal's 124x200 frame; a symbol fills about x 26..98, y 74..122 (centre 62, 98).
struct Glyph {
    using Point = std::pair<float, float>;

    struct Dot {
        float x, y, r;
    };

    std::vector<std::vector<Point>> strokes; // polylines (curves are flattened), stroked as glowing lines
    std::vector<Dot> dots;                   // small filled circles

    bool operator==(const Glyph& other) const;
};

// The symbol of a block type, by typeId; nullptr if it has none of its own (then use categoryGlyph())
const Glyph* blockGlyph(const char* typeId);

// A generic symbol for a category
const Glyph& categoryGlyph(Category category);

} // namespace namsayin
