// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "BlockImages.hpp"

namespace xythos {

// The LED drawn on pedal faces, in layers (PNG bytes, greys): the unlit LED in its bezel (with alpha) and, added over
// it while lit, the lit lens, the bloom and the glass's reflections. Each but the glass is split into a neutral part,
// drawn as is, and a colour part, added tinted with the LED's colour. LedImages.cpp is generated from resources/leds/
// by tools/embed_led_images.py, which says where each layer's circles are.
BuiltinImage ledOffImage();
BuiltinImage ledOffColourImage();
BuiltinImage ledLensImage();
BuiltinImage ledLensColourImage();
BuiltinImage ledBloomImage();
BuiltinImage ledBloomColourImage();
BuiltinImage ledGlassImage();

} // namespace xythos
