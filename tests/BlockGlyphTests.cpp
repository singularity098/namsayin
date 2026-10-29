// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "engine/BlockRegistry.hpp"
#include "ui/BlockGlyphs.hpp"

#include <string>

using namespace xythos;

namespace {

bool isPedal(const BlockDescriptor& type) { return type.category != Category::Nam && type.category != Category::Ir; }

} // namespace

TEST_CASE("every pedal has a symbol of its own, inside its face")
{
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        if (!isPedal(*type))
            continue;
        CAPTURE(std::string(type->typeId));
        const Glyph* glyph = blockGlyph(type->typeId);
        REQUIRE(glyph != nullptr);
        REQUIRE(!glyph->strokes.empty());
        for (const auto& stroke : glyph->strokes)
        {
            CHECK(stroke.size() >= 2);
            for (const auto& [x, y] : stroke)
            {
                // clear of the knobs above and the name below
                CHECK(x >= 20.0f);
                CHECK(x <= 104.0f);
                CHECK(y >= 64.0f);
                CHECK(y <= 126.0f);
            }
        }
    }
}

TEST_CASE("no two pedals look the same")
{
    const auto& types = BlockRegistry::builtin().types();
    for (size_t a = 0; a < types.size(); ++a)
    {
        for (size_t b = a + 1; b < types.size(); ++b)
        {
            if (!isPedal(*types[a]) || !isPedal(*types[b]))
                continue;
            CAPTURE(std::string(types[a]->typeId));
            CAPTURE(std::string(types[b]->typeId));
            CHECK_FALSE(*blockGlyph(types[a]->typeId) == *blockGlyph(types[b]->typeId));
        }
    }
}

TEST_CASE("unknown types fall back to their category's symbol")
{
    CHECK(blockGlyph("no-such-block") == nullptr);
    CHECK(!categoryGlyph(Category::Delay).strokes.empty());
}
