// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/TempoSync.hpp"

#include <algorithm>
#include <cmath>

namespace xythos {

namespace {

struct Note {
    const char* name;
    double beats;
};

constexpr Note kNotes[kNoteCount] = {
    { "1/1", 4.0 },       { "1/2.", 3.0 },        { "1/2", 2.0 },   { "1/4.", 1.5 },  { "1/2T", 4.0 / 3.0 },
    { "1/4", 1.0 },       { "1/8.", 0.75 },       { "1/4T", 2.0 / 3.0 }, { "1/8", 0.5 }, { "1/16.", 0.375 },
    { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },      { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },
};

} // namespace

const char* noteName(int index)
{
    return kNotes[std::clamp(index, 0, kNoteCount - 1)].name;
}

double noteBeats(int index)
{
    return kNotes[std::clamp(index, 0, kNoteCount - 1)].beats;
}

int timeNoteIndex(float normalized)
{
    const float n = std::clamp(normalized, 0.0f, 1.0f);
    return kNoteCount - 1 - static_cast<int>(std::lround(n * (kNoteCount - 1)));
}

int rateNoteIndex(float normalized)
{
    const float n = std::clamp(normalized, 0.0f, 1.0f);
    return static_cast<int>(std::lround(n * (kRateNoteCount - 1)));
}

const char* rateNoteName(int index)
{
    if (index <= 0)
        return "4 bars";
    if (index == 1)
        return "2 bars";
    return noteName(index - 2);
}

double rateNoteBeats(int index)
{
    return index <= 0 ? 16.0 : (index == 1 ? 8.0 : noteBeats(index - 2));
}

double rateNoteHz(int index, double bpm)
{
    return bpm / 60.0 / rateNoteBeats(index);
}

} // namespace xythos
