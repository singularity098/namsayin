// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "engine/BlockRegistry.hpp"
#include "ui/PedalFaces.hpp"

#include <cstring>
#include <string>

using namespace namsayin;

TEST_CASE("pedal face knobs turn real knob parameters, inside their image")
{
    for (const PedalFace& face : pedalFaces())
    {
        CAPTURE(std::string(face.typeId));
        const BlockDescriptor* type = BlockRegistry::builtin().find(face.typeId);
        REQUIRE(type != nullptr);
        CHECK(pedalFace(face.typeId) == &face);
        REQUIRE(!face.knobs.empty());
        for (const FaceKnob& knob : face.knobs)
        {
            CAPTURE(std::string(knob.param));
            int found = -1;
            for (int p = 0; p < type->numParams; ++p)
                if (std::strcmp(type->params[p].name, knob.param) == 0)
                    found = p;
            REQUIRE(found >= 0);
            const ParamScale scale = type->params[found].scale;
            CHECK((scale == ParamScale::Linear || scale == ParamScale::Log));
            CHECK(knob.radius > 0.0f);
            CHECK(knob.x - knob.radius >= 0.0f);
            CHECK(knob.x + knob.radius <= 1.0f);
            CHECK(knob.y > 0.0f);
            CHECK(knob.y < 1.0f);
        }
    }
    CHECK(pedalFace("no-such-block") == nullptr);
}

TEST_CASE("pedal face LEDs sit inside their image")
{
    for (const PedalFace& face : pedalFaces())
    {
        CAPTURE(std::string(face.typeId));
        for (const FaceLed& led : face.leds)
        {
            CHECK(led.radius > 0.0f);
            CHECK(led.x - led.radius >= 0.0f);
            CHECK(led.x + led.radius <= 1.0f);
            CHECK(led.y > 0.0f);
            CHECK(led.y < 1.0f);
        }
    }
    REQUIRE(pedalFace("polychip") != nullptr);
    CHECK(pedalFace("polychip")->leds.size() == 2);
}
