// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "BlockImages.hpp"

namespace namsayin {

// The knob drawn on pedal faces (PNG bytes, pointing up) and its lighting layer (white, alpha = light), drawn over it
// unrotated. KnobImages.cpp is generated from resources/knobs/ by tools/embed_knob_images.py.
BuiltinImage knobImage();
BuiltinImage knobSheenImage();

// The knob with a glowing rim, in two parts: the neutral knob (with alpha, out to the rim) and the rim's colour and glow
// (grey), added tinted with the pedal's colour (PedalFaces.cpp). The rim's outer edge is 1 / kRimKnobGlow of the
// images' half-width.
constexpr float kRimKnobGlow = 1.08f;
BuiltinImage rimKnobImage();
BuiltinImage rimKnobColourImage();

} // namespace namsayin
