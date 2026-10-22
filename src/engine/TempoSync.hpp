// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

namespace namsayin {

// Note values for tempo-synced knobs, longest first: 1/1 down to 1/32, with dotted and triplet values
constexpr int kNoteCount = 14;

const char* noteName(int index);
double noteBeats(int index); // length in quarter notes

// The note a normalized knob position picks. A time knob has the longest note at the top; a rate knob the
// shortest (fastest).
int timeNoteIndex(float normalized);
int rateNoteIndex(float normalized);

// One cycle per note
inline double noteHz(int index, double bpm)
{
    return bpm / 60.0 / noteBeats(index);
}

} // namespace namsayin
