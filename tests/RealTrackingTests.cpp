// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// The tracking benchmark on real playing: the owner's DI recordings (tests/recordings: chords, power chords, octaves,
// a ringing arpeggio, a riff, palm-muted chugs, bends) against hand-made note lists (<clip>.notes). Decode them first
// with tools/decode_recordings.sh (ffmpeg), then:
//   NAMSAYIN_TRACKING_BENCH=1 ./build/namsayin-tests -tc="*real recordings*"
// NAMSAYIN_TRACKING_DEBUG=1 lists each miss and stray note.
//
// A .notes line is "start end notes" (seconds): a note listed is played and must sound (after a grace for its band
// to ring up, scored as delay), ?X may sound or not (a doubling or harmonic the recording can't settle), A|B is one of
// them (bends), and * is anything. Any other note sounding is a stray note.

#include "TestSupport.hpp"

#include "engine/dsp/NoteTracker.hpp"
#include "engine/dsp/PolyNoteFinder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace namsayin;

namespace {

using Notes = std::bitset<128>; // by MIDI note

// The lowest note in a set (std::bitset has no portable find-first)
int lowestNote(const Notes& notes)
{
    for (size_t n = 0; n < notes.size(); ++n)
        if (notes[n])
            return static_cast<int>(n);
    return 0;
}

constexpr double kRate = 48000.0;

struct Segment {
    double start = 0.0, end = 0.0;
    Notes played, maybe;
    std::vector<Notes> groups; // one of each must sound
    bool any = false;
};

struct Clip {
    std::string name;
    std::vector<float> signal;
    std::vector<Segment> segments;
};

int parseNote(const std::string& s)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int pc = 11; pc >= 0; --pc)
    {
        const size_t len = std::strlen(names[pc]);
        if (s.compare(0, len, names[pc]) == 0 && s.size() > len && (s[len] == '-' || std::isdigit(s[len])))
            return pc + 12 * (std::stoi(s.substr(len)) + 1);
    }
    return -1;
}

std::string noteName(int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string(names[midi % 12]) + std::to_string(midi / 12 - 1);
}

// A float WAV as tools/decode_recordings.sh writes it (mono, 32-bit float, 48 kHz)
bool loadWav(const std::string& path, std::vector<float>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t i = 12;
    while (i + 8 <= data.size())
    {
        uint32_t size = 0;
        std::memcpy(&size, &data[i + 4], 4);
        if (std::memcmp(&data[i], "data", 4) == 0)
        {
            out.resize(std::min<size_t>(size, data.size() - i - 8) / 4);
            std::memcpy(out.data(), &data[i + 8], out.size() * 4);
            return true;
        }
        i += 8 + size + (size & 1);
    }
    return false;
}

std::vector<Segment> loadNotes(const std::string& path)
{
    std::vector<Segment> segments;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        // comments start with '#' after a space (a sharp follows a letter)
        if (!line.empty() && line[0] == '#')
            continue;
        line = line.substr(0, line.find(" #"));
        std::istringstream in(line);
        Segment seg;
        if (!(in >> seg.start >> seg.end))
            continue;
        std::string token;
        while (in >> token)
        {
            if (token == "*")
                seg.any = true;
            else if (token[0] == '?')
                seg.maybe.set(static_cast<size_t>(parseNote(token.substr(1))));
            else if (token.find('|') != std::string::npos)
            {
                Notes group;
                std::istringstream parts(token);
                std::string part;
                while (std::getline(parts, part, '|'))
                    group.set(static_cast<size_t>(parseNote(part)));
                seg.groups.push_back(group);
            }
            else
                seg.played.set(static_cast<size_t>(parseNote(token)));
        }
        segments.push_back(seg);
    }
    return segments;
}

const std::vector<Clip>& clips()
{
    static const std::vector<Clip> all = [] {
        std::vector<Clip> list;
        const char* dir = std::getenv("NAMSAYIN_RECORDINGS");
        const std::string wavs = dir ? dir : NAMSAYIN_RECORDINGS_WAV;
        for (const char* name : { "open-g-chord", "open-d-chord", "power-chords", "octave-shapes", "ringing-arpeggio",
                                  "slow-riff", "chugging-riff", "string-bends" })
        {
            Clip clip { name, {}, loadNotes(std::string(NAMSAYIN_RECORDINGS) + "/" + name + ".notes") };
            if (!loadWav(wavs + "/" + name + ".wav", clip.signal))
                continue;
            list.push_back(std::move(clip));
        }
        return list;
    }();
    return all;
}

struct RealScore {
    int notes = 0, missed = 0;
    double medianDelayMs = 0.0;
    double missingMs = 0.0; // a played note not sounding (after its grace)
    double missingClassMs = 0.0; // ... nor in another octave
    int stray = 0;          // stretches of a note nobody played
    double strayMs = 0.0;
    double strayNeighbourMs = 0.0, strayHarmonicMs = 0.0, straySubMs = 0.0; // a semitone or two off, a harmonic, below
};

constexpr double kGraceMs = 60.0; // a new note's band ringing up: scored as delay, not as missing
constexpr double kTailMs = 40.0;  // a note may ring this long past its segment

// Scores the notes sounding at each sample against the clip's note list. `mono`: one note at a time, so a chord
// only needs one of its notes (any played or maybe note counts).
RealScore score(const Clip& clip, const std::vector<Notes>& out, size_t latency, bool mono, bool debug)
{
    RealScore s;
    const size_t length = clip.signal.size();
    auto played = [&](size_t t) { return t + latency < out.size() ? out[t + latency] : Notes {}; };
    auto at = [](double sec) { return static_cast<size_t>(sec * kRate); };
    const size_t grace = at(kGraceMs / 1000.0), tail = at(kTailMs / 1000.0);

    // what may sound at each sample: the segment's played and maybe notes, and the last segment's for a tail
    std::vector<Notes> allowed(length);
    std::vector<bool> anything(length, false);
    for (size_t k = 0; k < clip.segments.size(); ++k)
    {
        const Segment& seg = clip.segments[k];
        Notes ok = seg.played | seg.maybe;
        for (const Notes& g : seg.groups)
            ok |= g;
        for (size_t t = at(seg.start); t < std::min(length, at(seg.end) + tail); ++t)
        {
            allowed[t] |= ok;
            if (seg.any && t < at(seg.end))
                anything[t] = true;
        }
    }

    // played notes (and groups): found, how late, missing while held
    std::vector<double> delays;
    for (size_t k = 0; k < clip.segments.size(); ++k)
    {
        const Segment& seg = clip.segments[k];
        const Segment* before = k > 0 ? &clip.segments[k - 1] : nullptr;
        std::vector<Notes> wants;
        if (mono)
        {
            Notes all = seg.played;
            for (const Notes& g : seg.groups)
                all |= g;
            if (all.any())
                wants.push_back(all | seg.maybe);
        }
        else
        {
            for (int n = 0; n < 128; ++n)
                if (seg.played[static_cast<size_t>(n)])
                    wants.push_back(Notes().set(static_cast<size_t>(n)));
            for (const Notes& g : seg.groups)
                wants.push_back(g);
        }
        for (const Notes& want : wants)
        {
            // a note carried on from the segment before has no grace (and no new start to time)
            bool carried = false;
            if (before && seg.start - before->end < 0.001)
            {
                Notes prev = before->played | before->maybe;
                for (const Notes& g : before->groups)
                    prev |= g;
                carried = (prev & want).any();
            }
            const size_t from = at(seg.start), to = std::min(length, at(seg.end));
            size_t found = to;
            for (size_t t = from; t < to; ++t)
                if ((played(t) & want).any())
                {
                    found = t;
                    break;
                }
            if (!carried)
            {
                ++s.notes;
                if (found == to)
                {
                    ++s.missed;
                    if (debug)
                        std::printf("    missed %s at %.3f s\n", noteName(lowestNote(want)).c_str(),
                                    seg.start);
                }
                else
                    delays.push_back(1000.0 * static_cast<double>(found - from) / kRate);
            }
            double gapMs = 0.0;
            Notes classes; // the wanted notes in every octave
            for (int n = 0; n < 128; ++n)
                if (want[static_cast<size_t>(n)])
                    for (int o = n % 12; o < 128; o += 12)
                        classes.set(static_cast<size_t>(o));
            for (size_t t = carried ? from : from + grace; t < to; ++t)
            {
                if (!(played(t) & want).any())
                    gapMs += 1000.0 / kRate;
                if (!(played(t) & classes).any())
                    s.missingClassMs += 1000.0 / kRate;
            }
            if (debug && gapMs >= 20.0)
                std::printf("    %s missing %.0f ms in %.2f-%.2f s\n",
                            noteName(lowestNote(want)).c_str(), gapMs, seg.start, seg.end);
            s.missingMs += gapMs;
        }
    }
    if (!delays.empty())
    {
        std::sort(delays.begin(), delays.end());
        s.medianDelayMs = delays[delays.size() / 2];
    }

    // stray notes: per note, stretches of 3 ms or more (gaps under 5 ms joined)
    const size_t minimum = at(0.003), join = at(0.005);
    for (size_t n = 0; n < 128; ++n)
    {
        size_t begin = 0, end = 0;
        bool open = false;
        auto close = [&]() {
            if (open && end - begin >= minimum)
            {
                ++s.stray;
                if (debug)
                    std::printf("    stray %s at %.3f s for %.1f ms\n", noteName(static_cast<int>(n)).c_str(),
                                begin / kRate, 1000.0 * static_cast<double>(end - begin) / kRate);
            }
        };
        for (size_t t = 0; t < length; ++t)
        {
            if (!played(t)[n] || allowed[t][n] || anything[t])
                continue;
            s.strayMs += 1000.0 / kRate;
            auto near = [&](std::initializer_list<int> ds) {
                for (int d : ds)
                    if (static_cast<int>(n) + d >= 0 && static_cast<int>(n) + d < 128 && allowed[t][n + d])
                        return true;
                return false;
            };
            if (near({ -1, 1, -2, 2 }))
                s.strayNeighbourMs += 1000.0 / kRate;
            else if (near({ -12, -19, -24, -28, -31, -34, -36 }))
                s.strayHarmonicMs += 1000.0 / kRate;
            else if (near({ 12, 19, 24 }))
                s.straySubMs += 1000.0 / kRate;
            if (!open || t > end + join)
            {
                close();
                begin = t;
                open = true;
            }
            end = t + 1;
        }
        close();
    }
    return s;
}

// A tracker under test: fills `out` with the notes sounding at each sample; returns its latency
using RealTracker = std::function<size_t(const std::vector<float>&, std::vector<Notes>&)>;

constexpr float kAudible = 0.01f; // a voice's level (band level x 5, as the Poly Chip plays it)
constexpr float kRealGate = 0.03f; // the recordings are DI, about 30 dB quieter than the synthetic guitar

// Hiss and mains hum (60 Hz and its 3rd harmonic), each at `level` (RMS of the hiss, peak of the hum), as a live
// input might have under the guitar
float liveNoise(size_t t, float level, uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    const float hiss = (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * 1.732f;
    const double s = static_cast<double>(t) / kRate;
    const float hum = static_cast<float>(std::sin(2.0 * M_PI * 60.0 * s) + 0.5 * std::sin(2.0 * M_PI * 180.0 * s));
    return level * (hiss + hum);
}

RealTracker polyFinder(double lookaheadMs, float noise = 0.0f)
{
    return [lookaheadMs, noise](const std::vector<float>& x, std::vector<Notes>& out) {
        PolyNoteFinder finder;
        finder.prepare(kRate);
        finder.setLookahead(lookaheadMs);
        finder.setVoices(6);
        finder.setFocus(std::exp2(6.0f * 0.7f - 3.0f)); // the Poly Chip's default, 70%
        finder.setPickFilter(15.0f);
        out.assign(x.size(), Notes {});
        Notes now;
        uint32_t seed = 1;
        for (size_t t = 0; t < x.size(); ++t)
        {
            if (finder.process(x[t] + (noise > 0.0f ? liveNoise(t, noise, seed) : 0.0f)))
            {
                const PolyNoteFinder::Frame& f = finder.playFrame();
                now.reset();
                for (int b = 0; b < PolyNoteFinder::kBands; ++b)
                    if (f.sounding[b] && f.level[b] * 5.0f > kAudible * kRealGate)
                        now.set(static_cast<size_t>(b + PolyNoteFinder::kFirstNote));
            }
            out[t] = now;
        }
        return static_cast<size_t>(finder.latency());
    };
}

RealTracker monoTracker(float pickMs, double lookaheadMs)
{
    return [pickMs, lookaheadMs](const std::vector<float>& x, std::vector<Notes>& out) {
        NoteTracker tracker;
        tracker.prepare(kRate);
        tracker.setPickFilter(pickMs);
        tracker.setLookahead(lookaheadMs);
        out.assign(x.size(), Notes {});
        for (size_t t = 0; t < x.size(); ++t)
        {
            const NoteTracker::Note& n = tracker.process(x[t]);
            if (n.on && n.hz > 0.0)
            {
                const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(n.hz / 440.0)));
                if (midi >= 0 && midi < 128)
                    out[t].set(static_cast<size_t>(midi));
            }
        }
        return static_cast<size_t>(tracker.latency());
    };
}

std::vector<RealScore> realBench(const char* name, const RealTracker& tracker, bool mono, bool print = true)
{
    const bool debug = std::getenv("NAMSAYIN_TRACKING_DEBUG") != nullptr && print;
    if (print)
    {
        std::printf("\n%s\n", name);
        std::printf("  %-18s %5s %6s %9s %10s %6s %9s\n", "clip", "notes", "missed", "delay ms", "missing ms", "stray",
                    "stray ms");
    }
    std::vector<RealScore> scores;
    RealScore total;
    for (const Clip& clip : clips())
    {
        if (debug)
            std::printf("  %s\n", clip.name.c_str());
        std::vector<Notes> out;
        const size_t latency = tracker(clip.signal, out);
        const RealScore s = score(clip, out, latency, mono, debug);
        if (print)
            std::printf("  %-18s %5d %6d %9.1f %10.0f %6d %9.0f  (%.0f %.0f %.0f %.0f) pc %.0f\n", clip.name.c_str(),
                        s.notes, s.missed, s.medianDelayMs, s.missingMs, s.stray, s.strayMs, s.strayNeighbourMs,
                        s.strayHarmonicMs, s.straySubMs,
                        s.strayMs - s.strayNeighbourMs - s.strayHarmonicMs - s.straySubMs, s.missingClassMs);
        scores.push_back(s);
        total.notes += s.notes;
        total.missed += s.missed;
        total.missingMs += s.missingMs;
        total.missingClassMs += s.missingClassMs;
        total.stray += s.stray;
        total.strayMs += s.strayMs;
        total.strayNeighbourMs += s.strayNeighbourMs;
        total.strayHarmonicMs += s.strayHarmonicMs;
        total.straySubMs += s.straySubMs;
    }
    if (print)
        std::printf("  missing ms in no octave: %.0f\n", total.missingClassMs);
    if (print)
        std::printf("  stray ms: %.0f a semitone or two off, %.0f harmonics, %.0f below a note, %.0f other\n",
                    total.strayNeighbourMs, total.strayHarmonicMs, total.straySubMs,
                    total.strayMs - total.strayNeighbourMs - total.strayHarmonicMs - total.straySubMs);
    if (print)
        std::printf("  %-18s %5d %6d %9s %10.0f %6d %9.0f\n", "total", total.notes, total.missed, "", total.missingMs,
                    total.stray, total.strayMs);
    scores.push_back(total);
    return scores;
}

} // namespace

TEST_CASE("tracking benchmark on real recordings" * doctest::skip(std::getenv("NAMSAYIN_TRACKING_BENCH") == nullptr))
{
    if (clips().empty())
    {
        std::printf("no decoded recordings: run tools/decode_recordings.sh\n");
        return;
    }
    realBench("poly note finder", polyFinder(0.0), false);
    realBench("poly note finder, lookahead 20 ms", polyFinder(20.0), false);
    realBench("poly note finder, with hiss and hum at -66 dB", polyFinder(0.0, 0.0005f), false);
    realBench("note tracker (mono)", monoTracker(0.0f, 0.0), true);
    realBench("note tracker (mono), lookahead 15 ms", monoTracker(0.0f, 15.0), true);
}

TEST_CASE("the poly note finder on the owner's recordings")
{
    if (clips().empty())
    {
        MESSAGE("no decoded recordings (tools/decode_recordings.sh): skipped");
        return;
    }
    // before finding notes by their harmonics (2026-10-24): 7 missed, 30.1 s of missing note time (7.3 s in no
    // octave), 255 stray notes (9.7 s). Before the bands' own noise floors and holding notes briefly not found
    // (2026-10-25): 5 missed, 19.5 s missing (5.8 s in no octave), 187 stray (6.1 s); with hiss and hum at -66 dB
    // (the input's noise floor gated the notes 12 dB over it): 8 missed, 25.6 s missing (10.2 s in no octave)
    const auto plain = realBench("poly note finder", polyFinder(0.0), false, false);
    CHECK(plain.back().missed <= 5);
    CHECK(plain.back().missingMs <= 16500.0);
    CHECK(plain.back().missingClassMs <= 5500.0);
    CHECK(plain.back().stray <= 200);
    CHECK(plain.back().strayMs <= 8000.0);
    // a live input's hiss and hum don't cut the notes short
    const auto noisy = realBench("poly note finder, with hiss and hum", polyFinder(0.0, 0.0005f), false, false);
    CHECK(noisy.back().missed <= 5);
    CHECK(noisy.back().missingMs <= 16500.0);
    CHECK(noisy.back().missingClassMs <= 5500.0);
    CHECK(noisy.back().strayMs <= 8000.0);
}

TEST_CASE("the note tracker on the owner's recordings")
{
    if (clips().empty())
    {
        MESSAGE("no decoded recordings (tools/decode_recordings.sh): skipped");
        return;
    }
    // before the check for a chord's octave below its root (2026-10-24): 19.8 s missing, 146 stray notes (15.6 s,
    // 13.0 s of it an octave or a fifth below a note)
    const auto mono = realBench("note tracker (mono)", monoTracker(0.0f, 0.0), true, false);
    CHECK(mono.back().missed == 0);
    CHECK(mono.back().missingMs <= 14000.0);
    CHECK(mono.back().stray <= 130);
    CHECK(mono.back().strayMs <= 7000.0);
    CHECK(mono.back().straySubMs <= 4500.0);
}
