// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>

namespace namsayin {

// Note values for tempo-synced knobs, longest first: 1/1 down to 1/32, with dotted and triplet values
constexpr int kNoteCount = 14;

const char* noteName(int index);
double noteBeats(int index); // length in quarter notes

// The note a time knob's normalized position picks: the longest note at the top
int timeNoteIndex(float normalized);

// Rates (LFOs) have their own list: two and four bars, then the notes above. Index 0 is the slowest; a rate knob
// has the fastest at the top.
constexpr int kRateNoteCount = kNoteCount + 2;
int rateNoteIndex(float normalized);
const char* rateNoteName(int index);
double rateNoteBeats(int index);          // length of one cycle in quarter notes
double rateNoteHz(int index, double bpm); // one cycle per note

// The phase (0..1) of a cycle of `cycleBeats` at song position `beats`, cycles counted from the song's start
inline double songPhase(double beats, double cycleBeats)
{
    const double cycles = beats / cycleBeats;
    return cycles - std::floor(cycles);
}

} // namespace namsayin
