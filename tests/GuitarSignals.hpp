// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

// A synthetic electric guitar for testing pitch tracking: six plucked strings (Karplus-Strong with a fractional
// delay, from a pick-shaped start, read as velocity like a magnetic pickup), with the pick's click, fretting-hand
// muting, hammer-ons, bends, strings left ringing, mains hum and hiss. A score of notes gives both the signal and
// the truth: which note sounds at each sample.
namespace guitar {

constexpr double kRate = 48000.0;

inline double midiHz(double midi) { return 440.0 * std::exp2((midi - 69.0) / 12.0); }

struct Note {
    double midi = 52.0;
    double start = 0.0; // seconds
    double length = 0.3; // seconds until the note is muted (or until the next note, for a ring)
    int string = 0;     // 0..5; a new note on a string replaces the one it was playing
    float velocity = 1.0f;
    bool legato = false; // a hammer-on or pull-off: the string changes pitch without a new pick
    bool ring = false;   // left ringing (not muted) when it ends; its truth still ends at start + length
    bool palmMute = false; // a short, dull, palm-muted note
    double bend = 0.0;   // semitones, reached over the first half of the note and held
    double vibrato = 0.0; // cents, 5.5 Hz, from a third of the way in
};

// What should be heard at a sample
enum class Truth : uint8_t { Silent, Note, DontCare };

struct Performance {
    std::vector<float> signal;
    std::vector<Truth> truth;
    std::vector<float> midi; // the note at each sample (only where truth is Note)
    std::vector<Note> notes;
};

struct Options {
    float hum = 0.0005f;  // mains hum at 50 Hz with harmonics (about -66 dBFS)
    float hiss = 0.0003f;
    float level = 0.35f;  // pickup output for a full pick
    uint32_t seed = 1;
};

// How long a note counts as played
inline double truthLength(const Note& note) { return note.palmMute ? std::min(note.length, 0.06) : note.length; }

inline Performance perform(const std::vector<Note>& notes, double seconds, const Options& options = {})
{
    const auto length = static_cast<size_t>(seconds * kRate);
    Performance p;
    p.signal.assign(length, 0.0f);
    p.truth.assign(length, Truth::Silent);
    p.midi.assign(length, 0.0f);
    p.notes = notes;
    std::mt19937 rng(options.seed);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);

    constexpr int kLine = 4096;
    struct String {
        std::vector<float> line = std::vector<float>(kLine, 0.0f);
        int write = 0;
        float previous = 0.0f; // the loop filter's last input
        float last = 0.0f;     // the last output, for the velocity
        float tone = 1.0f;     // loop filter weight (1 = plain averaging; lower = duller, for palm mutes)
        int note = -1;         // index of the note it's playing
    };
    String strings[6];

    // the notes, in time order of their sample index
    std::vector<size_t> order(notes.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
    size_t next = 0;

    for (size_t t = 0; t < length; ++t)
    {
        const double time = static_cast<double>(t) / kRate;
        while (next < order.size() && static_cast<size_t>(notes[order[next]].start * kRate) <= t)
        {
            const size_t n = order[next++];
            const Note& note = notes[n];
            String& s = strings[note.string];
            s.note = static_cast<int>(n);
            s.tone = note.palmMute ? 0.6f : 1.0f;
            if (!note.legato)
            {
                // a pick: a triangle (plucked at a fifth of the string) over one period, on top of what rings
                const double period = kRate / midiHz(note.midi);
                const auto samples = static_cast<int>(period);
                const double pick = 0.2 * period;
                // (the displacement starts and ends at zero, so the string doesn't jump: the pickup reads velocity)
                for (int k = 0; k < samples; ++k)
                {
                    const double x = k;
                    const auto shape = static_cast<float>(x < pick ? x / pick : (period - x) / (period - pick));
                    float& v = s.line[static_cast<size_t>((s.write - 1 - k + 2 * kLine) % kLine)];
                    v = 0.15f * v + note.velocity * shape * (1.0f + 0.05f * uniform(rng));
                }
            }
        }

        float out = 0.0f;
        for (String& s : strings)
        {
            if (s.note < 0)
                continue;
            const Note& note = notes[static_cast<size_t>(s.note)];
            const double age = time - note.start;
            double midi = note.midi;
            if (note.bend != 0.0)
                midi += note.bend * std::min(1.0, age / (0.5 * note.length));
            if (note.vibrato != 0.0 && age > note.length / 3.0)
                midi += note.vibrato / 100.0 * std::sin(2.0 * M_PI * 5.5 * (age - note.length / 3.0));
            const double hz = midiHz(midi);
            // the loop's delay: the period less the loop filter's delay
            const double delay = kRate / hz - (1.0 - 0.5 * s.tone);
            const auto whole = static_cast<int>(delay);
            const double frac = delay - whole;
            const float a = s.line[static_cast<size_t>((s.write - whole + kLine) % kLine)];
            const float b = s.line[static_cast<size_t>((s.write - whole - 1 + kLine) % kLine)];
            const float v = static_cast<float>(a + (b - a) * frac);
            // decay: low notes ring about 6 s, high ones shorter; muted strings die in about 30 ms
            const bool muted = !note.ring && age > note.length;
            double t60 = note.palmMute ? 0.12 : 6.0 * std::sqrt(82.4 / hz);
            if (muted)
                t60 = 0.03;
            const auto loss = static_cast<float>(std::pow(0.001, 1.0 / (t60 * hz)));
            const float filtered = loss * (s.tone * 0.5f * (v + s.previous) + (1.0f - s.tone) * s.previous);
            s.previous = v;
            s.line[static_cast<size_t>(s.write)] = filtered;
            s.write = (s.write + 1) % kLine;
            const float velocity = (v - s.last) * static_cast<float>(kRate / hz) / 8.0f;
            s.last = v;
            out += velocity;
        }

        // the pick's click: 2 ms of decaying noise at each pick
        float click = 0.0f;
        for (const Note& note : notes)
        {
            const double age = time - note.start;
            if (!note.legato && age >= 0.0 && age < 0.002)
                click += 0.3f * note.velocity * uniform(rng) * static_cast<float>(1.0 - age / 0.002);
        }
        float hum = 0.0f;
        for (int h = 1; h <= 7; h += 2)
            hum += std::sin(static_cast<float>(2.0 * M_PI * 50.0 * h * time)) / static_cast<float>(h);
        p.signal[t] = options.level * (out + click) + options.hum * hum + options.hiss * uniform(rng);
    }

    // truth: the newest note in its time sounds; 25 ms after a mute is don't-care (the string still dies away), and
    // so is everything after a note left ringing
    // (a palm-muted note is 30 dB down after 60 ms: after that, it's don't-care too)
    for (const Note& note : notes)
    {
        const auto to = std::min(length, static_cast<size_t>((note.start + truthLength(note)) * kRate));
        const auto muted = static_cast<size_t>((note.start + note.length + 0.025) * kRate);
        const auto tail = note.ring ? length : std::min(length, muted);
        for (size_t t = to; t < tail; ++t)
            if (p.truth[t] == Truth::Silent)
                p.truth[t] = Truth::DontCare;
    }
    std::vector<size_t> byStart = order;
    for (size_t n : byStart)
    {
        const Note& note = notes[n];
        const auto from = static_cast<size_t>(note.start * kRate);
        const auto to = std::min(length, static_cast<size_t>((note.start + truthLength(note)) * kRate));
        for (size_t t = from; t < to; ++t)
        {
            const double age = static_cast<double>(t) / kRate - note.start;
            double midi = note.midi;
            if (note.bend != 0.0)
                midi += note.bend * std::min(1.0, age / (0.5 * note.length));
            if (note.vibrato != 0.0 && age > note.length / 3.0)
                midi += note.vibrato / 100.0 * std::sin(2.0 * M_PI * 5.5 * (age - note.length / 3.0));
            p.truth[t] = Truth::Note;
            p.midi[t] = static_cast<float>(midi);
        }
    }
    return p;
}

// A high-gain amp in front of the tracker: 30 dB of gain into a soft clipper, a bit of asymmetry, then a tone
// low-pass
inline std::vector<float> throughAmp(const std::vector<float>& x)
{
    std::vector<float> y(x.size());
    float lp = 0.0f;
    const float coef = 1.0f - std::exp(static_cast<float>(-2.0 * M_PI * 5000.0 / kRate));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float driven = std::tanh(31.6f * x[i] + 0.2f) - std::tanh(0.2f);
        lp += (driven - lp) * coef;
        y[i] = 0.3f * lp;
    }
    return y;
}

// The scores the tracking benchmark plays
inline std::vector<Note> singleNotes()
{
    // every few semitones from low E to the 22nd fret of the high E, each picked and muted, in turn on its string
    std::vector<Note> notes;
    double time = 0.1;
    const int open[6] = { 40, 45, 50, 55, 59, 64 };
    for (int midi = 40; midi <= 86; midi += 2)
    {
        int string = 0;
        for (int s = 5; s >= 0; --s)
            if (midi >= open[s])
            {
                string = s;
                break;
            }
        Note note;
        note.midi = midi;
        note.start = time;
        note.length = 0.35;
        note.string = string;
        note.velocity = 0.5f + 0.5f * static_cast<float>((midi * 7) % 5) / 4.0f;
        notes.push_back(note);
        time += 0.5;
    }
    return notes;
}

inline std::vector<Note> fastRun()
{
    // 16th notes at 140 BPM, picked on alternate strings and muted by the next note: a two-octave G major scale up
    // and down
    const int scale[] = { 43, 45, 47, 48, 50, 52, 54, 55, 57, 59, 60, 62, 64, 66, 67 };
    std::vector<Note> notes;
    double time = 0.1;
    const double step = 60.0 / 140.0 / 4.0;
    int count = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < 15; ++i)
        {
            const int midi = pass == 0 ? scale[i] : scale[14 - i];
            Note note;
            note.midi = midi;
            note.start = time;
            note.length = step;
            note.string = std::min(5, (midi - 40) / 5);
            note.velocity = (count++ % 2) ? 0.7f : 1.0f;
            notes.push_back(note);
            time += step;
        }
    }
    return notes;
}

inline std::vector<Note> legatoAndRepeats()
{
    std::vector<Note> notes;
    double time = 0.1;
    // hammer-ons and pull-offs on the G string: pick, hammer, hammer, pull
    for (int phrase = 0; phrase < 4; ++phrase)
    {
        const int base = 55 + 2 * phrase;
        const int steps[] = { 0, 2, 4, 2 };
        for (int k = 0; k < 4; ++k)
        {
            Note note;
            note.midi = base + steps[k];
            note.start = time;
            note.length = 0.15;
            note.string = 3;
            note.legato = k > 0;
            notes.push_back(note);
            time += 0.15;
        }
        time += 0.2;
    }
    // the same note picked repeatedly (eighths at 120 BPM)
    for (int k = 0; k < 8; ++k)
    {
        Note note;
        note.midi = 57;
        note.start = time;
        note.length = 0.24;
        note.string = 2;
        note.velocity = (k % 2) ? 0.6f : 1.0f;
        notes.push_back(note);
        time += 0.25;
    }
    return notes;
}

inline std::vector<Note> ringingArpeggio()
{
    // a chord picked one string at a time, each left ringing: the newest note is the one to play
    std::vector<Note> notes;
    double time = 0.1;
    const int chords[3][4] = { { 45, 52, 57, 61 }, { 43, 50, 55, 59 }, { 40, 47, 52, 56 } };
    for (const auto& chord : chords)
    {
        for (int k = 0; k < 4; ++k)
        {
            Note note;
            note.midi = chord[k];
            note.start = time;
            note.length = 0.3;
            note.string = k + 1;
            note.ring = true;
            note.velocity = 0.8f;
            notes.push_back(note);
            time += 0.3;
        }
    }
    // then let the last chord die away, all strings ringing, then muted after 2 s (don't-care: chords)
    return notes;
}

inline std::vector<Note> palmMutes()
{
    // chugs on the low strings: palm-muted eighths with gaps, and a few open notes between
    std::vector<Note> notes;
    double time = 0.1;
    for (int k = 0; k < 24; ++k)
    {
        Note note;
        note.midi = (k % 8 == 7) ? 47 : ((k % 4 == 3) ? 43 : 40);
        note.start = time;
        note.length = 0.12;
        note.string = note.midi >= 45 ? 1 : 0;
        note.palmMute = true;
        note.velocity = 0.6f + 0.4f * static_cast<float>(k % 3) / 2.0f;
        notes.push_back(note);
        time += 0.25;
    }
    return notes;
}

inline std::vector<Note> bendsAndVibrato()
{
    std::vector<Note> notes;
    double time = 0.1;
    const int bases[] = { 62, 69, 74, 57 };
    for (int k = 0; k < 4; ++k)
    {
        Note note;
        note.midi = bases[k];
        note.start = time;
        note.length = 1.2;
        note.string = bases[k] >= 64 ? 5 : 3;
        note.bend = (k % 2) ? 2.0 : 1.0;
        note.vibrato = 25.0;
        notes.push_back(note);
        time += 1.5;
    }
    return notes;
}

inline std::vector<Note> longDecay()
{
    // notes left to ring out for 4 s each (the truth only covers the first 2 s; after that the tracker may stop)
    std::vector<Note> notes;
    double time = 0.1;
    for (int midi : { 40, 52, 64 })
    {
        Note note;
        note.midi = midi;
        note.start = time;
        note.length = 2.0;
        note.string = (midi - 40) / 12 * 2;
        note.ring = true;
        notes.push_back(note);
        time += 4.0;
    }
    return notes;
}

// A 5-string bass and a 7-string's low end: single notes every semitone from the bass's low B (B0, 31 Hz) up to
// G2, each picked, held and muted
inline std::vector<Note> bassNotes()
{
    std::vector<Note> notes;
    double time = 0.1;
    for (int midi = 23; midi <= 43; ++midi)
    {
        Note note;
        note.midi = midi;
        note.start = time;
        note.length = 0.5;
        note.string = midi % 2;
        note.velocity = 0.6f + 0.4f * static_cast<float>((midi * 7) % 5) / 4.0f;
        notes.push_back(note);
        time += 0.7;
    }
    return notes;
}

// A bass line in eighths at 110 BPM on the low strings (each note muted by the next), with a hammer-on
inline std::vector<Note> bassLine()
{
    static const int line[] = { 23, 23, 35, 23, 28, 28, 33, 35, 26, 26, 38, 26, 31, 33, 33, 23 };
    std::vector<Note> notes;
    const double eighth = 60.0 / 110.0 / 2.0;
    double time = 0.1;
    for (int bar = 0; bar < 2; ++bar)
        for (size_t i = 0; i < std::size(line); ++i)
        {
            Note note;
            note.midi = line[i];
            note.start = time;
            note.length = eighth * 0.9;
            note.string = 0;
            note.velocity = i % 2 == 0 ? 1.0f : 0.75f;
            note.legato = i == 13; // the second A1 hammered from the G
            if (note.legato)
                notes.back().length = eighth;
            notes.push_back(note);
            time += eighth;
        }
    return notes;
}

// Open chords strummed down (12 ms from string to string), each held, then muted
inline std::vector<Note> strummedChords()
{
    static const std::vector<std::vector<int>> chords = {
        { 40, 47, 52, 56, 59, 64 }, // E
        { 45, 52, 57, 61, 64 },     // A
        { 50, 57, 62, 66 },         // D
        { 43, 47, 50, 55, 59, 67 }, // G
        { 48, 52, 55, 60, 64 },     // C
        { 45, 52, 57, 60, 64 },     // Am
    };
    std::vector<Note> notes;
    double time = 0.1;
    for (const auto& chord : chords)
    {
        for (size_t k = 0; k < chord.size(); ++k)
        {
            Note note;
            note.midi = chord[k];
            note.start = time + 0.012 * static_cast<double>(k);
            note.length = 0.9 - 0.012 * static_cast<double>(k);
            // the strings from the low E up: the lowest note of a chord on the lowest string it uses
            note.string = static_cast<int>(6 - chord.size() + k);
            note.velocity = 0.8f;
            notes.push_back(note);
        }
        time += 1.2;
    }
    return notes;
}

// Power chords (root, fifth, octave) changing every eighth note at 120 BPM, each muted by the next
inline std::vector<Note> powerChords()
{
    static const int roots[] = { 40, 43, 45, 43, 40, 47, 45, 43 };
    std::vector<Note> notes;
    double time = 0.1;
    for (int pass = 0; pass < 2; ++pass)
        for (const int root : roots)
        {
            const int low = root < 45 ? 0 : 1;
            for (int k = 0; k < 3; ++k)
            {
                Note note;
                note.midi = root + (k == 0 ? 0 : (k == 1 ? 7 : 12));
                note.start = time + 0.006 * k;
                note.length = 0.25 - 0.006 * k - 0.01;
                note.string = low + k;
                notes.push_back(note);
            }
            time += 0.25;
        }
    return notes;
}

} // namespace guitar
