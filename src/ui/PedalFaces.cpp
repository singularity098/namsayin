// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "PedalFaces.hpp"

#include <cstring>

namespace namsayin {

const std::vector<PedalFace>& pedalFaces()
{
    static const std::vector<PedalFace> faces = {
        // proof of concept (owner, 2026-10-25): the two knobs sit between the screen and the footswitch
        // the LEDs: above the footswitch, and the power light beside the screen
        { "polychip",
          { { "Crush", 0.335f, 0.635f, 0.09f }, { "Mix", 0.665f, 0.635f, 0.09f } },
          { { 0.4985f, 0.786f, 0.0185f, 0xf7, 0x10, 0x93 }, { 0.2157f, 0.3292f, 0.0155f, 0xe6, 0x3d, 0xaf } } },
    };
    return faces;
}

const PedalFace* pedalFace(const char* typeId)
{
    for (const PedalFace& face : pedalFaces())
        if (std::strcmp(face.typeId, typeId) == 0)
            return &face;
    return nullptr;
}

} // namespace namsayin
