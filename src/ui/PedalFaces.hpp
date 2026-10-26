// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <vector>

namespace namsayin {

// Knobs drawn on a block's built-in image (resources/blocks/), each turning one of the block's parameters: the same
// host parameter as its knob among the panel's controls, so turning either turns both. Positions are fractions of the
// image (x and the radius of its width, y of its height). Plain data, no DPF, so the tests can check it.
struct FaceKnob {
    const char* param; // the ParamSpec name
    float x, y, radius;
};

// An LED on a block's image: drawn lit while the block is on and dark while it's bypassed, over the image's own
// (x and the radius are fractions of the image's width, y of its height; the colour is the lit one)
struct FaceLed {
    float x, y, radius;
    unsigned char r, g, b;
};

struct PedalFace {
    const char* typeId;
    std::vector<FaceKnob> knobs;
    std::vector<FaceLed> leds;
};

// A block type's face knobs and LEDs, or nullptr if its image has none
const PedalFace* pedalFace(const char* typeId);

const std::vector<PedalFace>& pedalFaces();

} // namespace namsayin
