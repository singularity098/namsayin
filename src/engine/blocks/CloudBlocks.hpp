// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "engine/Block.hpp"

namespace xythos {

// Grain Cloud: plays back short grains of the last couple of seconds of your playing, scattered in time (Spray),
// some shifted in pitch (Pitch) and some backwards (Reverse), across the stereo field: single notes turn into a
// shimmering cloud. Each grain has a Hann window; Feedback writes the cloud back into the buffer it reads, and
// Freeze stops writing, so the cloud keeps playing what is there. No config.
extern const BlockDescriptor kGrainCloudBlockDescriptor;

enum GrainCloudParam {
    kGrainParamSize,     // ms, each grain's length
    kGrainParamDensity,  // grains per second
    kGrainParamDelay,    // ms, how far back the grains start
    kGrainParamSpray,    // ms, the random spread on top
    kGrainParamPitch,    // choice: grainPitchNames
    kGrainParamReverse,  // %, the share of grains played backwards
    kGrainParamFeedback, // %
    kGrainParamWidth,    // %
    kGrainParamFreeze,   // toggle
    kGrainParamMix,      // %
    kGrainParamCount
};

// Swarm: a dozen or so very short delays (up to Spread), each drifting randomly in pitch (Drift, at Speed) and
// fluttering in level (Buzz, at 25-110 Hz like wings), spread across the stereo field. A little gives a 12-string or
// choir shimmer; all the way, a swarm of bees. No config.
extern const BlockDescriptor kSwarmBlockDescriptor;

enum SwarmParam {
    kSwarmParamVoices, // choice: 4, 8, 12, 16
    kSwarmParamSpread, // ms
    kSwarmParamDrift,  // cents
    kSwarmParamSpeed,  // Hz
    kSwarmParamBuzz,   // %
    kSwarmParamTone,   // Hz, low-pass
    kSwarmParamWidth,  // %
    kSwarmParamMix,    // %
    kSwarmParamCount
};

} // namespace xythos
