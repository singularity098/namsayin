// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cmath>

namespace xythos {

// The Pick Filter of the monophonic synths: a new note from the pitch tracker sounds only once its pitch has held
// steady for a few ms, so the brief, unsteady pitch of the pick attack (stray high notes, octave jumps) never sounds.
// While a note sounds, its pitch follows the tracker freely (bends, vibrato); a jump of more than half a semitone is a
// new note and waits too, the old one playing on meanwhile. 0 ms passes every estimate straight through.
class PickFilter {
public:
    // Non-RT
    void prepare(double sampleRate)
    {
        fSampleRate = sampleRate;
        setMs(fMs);
        reset();
    }

    void setMs(float ms)
    {
        fMs = ms;
        fHold = static_cast<int>(std::lround(ms * 0.001 * fSampleRate));
    }

    void reset()
    {
        fHz = fCandidate = 0.0;
        fWaited = 0;
        fOpen = false;
    }

    // One sample: whether the input is sounding, and the tracker's estimate if it made one this sample (else 0).
    // Returns true when the accepted pitch changed.
    bool process(bool sounding, double estimate)
    {
        if (!sounding)
        {
            fOpen = false;
            fCandidate = 0.0;
            return false;
        }
        if (estimate > 0.0)
        {
            if (fOpen && near(estimate, fHz))
            {
                fHz = estimate;
                fCandidate = 0.0;
                return true;
            }
            if (fCandidate > 0.0 && near(estimate, fCandidate))
            {
                fCandidate = estimate;
            }
            else
            {
                fCandidate = estimate;
                fWaited = 0;
            }
        }
        if (fCandidate > 0.0 && fWaited++ >= fHold)
        {
            fHz = fCandidate;
            fCandidate = 0.0;
            fOpen = true;
            return true;
        }
        return false;
    }

    double hz() const { return fHz; } // the accepted pitch; 0 until one is found
    bool open() const { return fHold == 0 || fOpen; } // a note has been accepted and is still sounding
    bool active() const { return fHold > 0; }

private:
    static bool near(double a, double b) { return std::fabs(std::log2(a / b)) < 0.5 / 12.0; }

    double fSampleRate = 48000.0;
    float fMs = 0.0f;
    int fHold = 0;
    double fHz = 0.0, fCandidate = 0.0;
    int fWaited = 0;
    bool fOpen = false;
};

} // namespace xythos
