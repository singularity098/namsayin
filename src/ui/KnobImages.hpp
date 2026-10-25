// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "BlockImages.hpp"

namespace namsayin {

// The knob drawn on pedal faces (PNG bytes, pointing up) and its lighting layer (white, alpha = light), drawn over it
// unrotated. KnobImages.cpp is generated from resources/knobs/ by tools/embed_knob_images.py.
BuiltinImage knobImage();
BuiltinImage knobSheenImage();

} // namespace namsayin
