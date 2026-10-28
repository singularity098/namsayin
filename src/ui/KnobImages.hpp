// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "BlockImages.hpp"

namespace namsayin {

// The knob drawn on pedal faces (PNG bytes): its body without the pointer, drawn unrotated so its reflections stay put,
// the pointer alone (pointing up, same frame), rotated to the value over it, and the lighting layer (white, alpha =
// light), drawn over both unrotated. KnobImages.cpp is generated from resources/knobs/ by tools/embed_knob_images.py.
BuiltinImage knobImage();
BuiltinImage knobPointerImage();
BuiltinImage knobSheenImage();

// The knob with a glowing rim, in parts: the neutral knob without its pointer (with alpha, out to the rim), the pointer
// alone, and the rim's colour and glow (grey), added tinted with the pedal's colour (PedalFaces.cpp). The rim's outer edge is 1 / kRimKnobGlow of the
// images' half-width.
constexpr float kRimKnobGlow = 1.08f;
BuiltinImage rimKnobImage();
BuiltinImage rimKnobPointerImage();
BuiltinImage rimKnobColourImage();

} // namespace namsayin
