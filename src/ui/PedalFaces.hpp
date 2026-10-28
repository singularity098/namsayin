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

// The image's latching footswitch (for a treadle, its toe): clicking it turns the block on or off
struct FaceSwitch {
    float x, y, radius;
};

// The colour of the face knobs' glowing rim, tinted from the blue-rim knob (resources/knobs/rim.png); all zero:
// plain black knobs (knob.png)
struct FaceRim {
    unsigned char r, g, b;
    bool lit() const { return r != 0 || g != 0 || b != 0; }
};

struct PedalFace {
    const char* typeId;
    std::vector<FaceKnob> knobs;
    std::vector<FaceLed> leds;
    FaceSwitch footswitch;
    FaceRim rim;
};

// A block type's face knobs and LEDs, or nullptr if its image has none
const PedalFace* pedalFace(const char* typeId);

const std::vector<PedalFace>& pedalFaces();

} // namespace namsayin
