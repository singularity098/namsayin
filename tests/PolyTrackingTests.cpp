// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// The polyphonic tracking benchmark: the synthetic guitar (GuitarSignals.hpp) playing chords, riffs and single notes
// into the note finder of the Poly Chip and the MIDI Out's Poly mode (dsp/PolyNoteFinder.hpp), counting the notes it
// misses, the notes it plays that weren't played, how late notes start, and notes that drop out while held.

#include "GuitarSignals.hpp"
#include "TestSupport.hpp"

#include "engine/dsp/PolyNoteFinder.hpp"

#include <doctest/doctest.h>

#include <bitset>
#include <cstdio>
#include <functional>
#include <string>

using namespace namsayin;

namespace {

using Notes = std::bitset<64>; // one bit per band (MIDI note - 36)

constexpr float kAudible = 0.01f; // a voice's level (band level x 5, as the Poly Chip plays it)

struct PolyScene {
    const char* name;
    std::vector<guitar::Note> notes;
    double seconds;
    guitar::Performance performance;
    std::vector<Notes> truth, dontCare;
};

const std::vector<PolyScene>& polyScenes()
{
    static const std::vector<PolyScene> all = [] {
        std::vector<PolyScene> scenes;
        auto add = [&](const char* name, std::vector<guitar::Note> notes, double seconds) {
            PolyScene scene { name, notes, seconds, guitar::perform(notes, seconds), {}, {} };
            const size_t length = scene.performance.signal.size();
            scene.truth.assign(length, Notes {});
            scene.dontCare.assign(length, Notes {});
            for (const guitar::Note& note : notes)
            {
                const int band = static_cast<int>(note.midi) - PolyNoteFinder::kFirstNote;
                const auto from = static_cast<size_t>(note.start * guitar::kRate);
                const auto to = std::min(length, static_cast<size_t>((note.start + guitar::truthLength(note)) * guitar::kRate));
                const auto tail = note.ring ? length
                                            : std::min(length, static_cast<size_t>((note.start + note.length + 0.025)
                                                                                   * guitar::kRate));
                for (size_t t = from; t < to; ++t)
                    scene.truth[t].set(static_cast<size_t>(band));
                for (size_t t = to; t < tail; ++t)
                    scene.dontCare[t].set(static_cast<size_t>(band));
                // the pick's first 30 ms: the band rings up, so the note may not be found yet (scored as delay)
            }
            scenes.push_back(std::move(scene));
        };
        add("strummed chords", guitar::strummedChords(), 7.5);
        add("power chords", guitar::powerChords(), 4.3);
        add("single notes", guitar::singleNotes(), 12.5);
        add("ringing arpeggio", guitar::ringingArpeggio(), 4.5);
        add("palm mutes", guitar::palmMutes(), 6.5);
        add("fast run", guitar::fastRun(), 3.5);
        return scenes;
    }();
    return all;
}

struct PolyScore {
    int notes = 0, missed = 0;
    double medianDelayMs = 0.0;
    int extra = 0;          // audible stretches of a note nobody is playing
    double extraMs = 0.0;   // summed over notes
    double dropoutMs = 0.0; // a held note not sounding, after it was found
};

// A finder under test: fills `out` with the notes sounding audibly at each sample; returns its latency
using PolyTracker = std::function<size_t(const std::vector<float>&, std::vector<Notes>&)>;

PolyTracker finderTracker(double lookaheadMs)
{
    return [lookaheadMs](const std::vector<float>& x, std::vector<Notes>& out) {
        PolyNoteFinder finder;
        finder.prepare(guitar::kRate);
        finder.setLookahead(lookaheadMs);
        finder.setVoices(6);
        finder.setFocus(std::exp2(6.0f * 0.7f - 3.0f)); // the Poly Chip's default, 70%
        finder.setPickFilter(15.0f);
        out.assign(x.size(), Notes {});
        Notes now;
        for (size_t t = 0; t < x.size(); ++t)
        {
            if (finder.process(x[t]))
            {
                const PolyNoteFinder::Frame& f = finder.playFrame();
                now.reset();
                for (int b = 0; b < PolyNoteFinder::kBands; ++b)
                    if (f.sounding[b] && f.level[b] * 5.0f > kAudible)
                        now.set(static_cast<size_t>(b));
            }
            out[t] = now;
        }
        if (std::getenv("NAMSAYIN_TRACKING_DEBUG"))
            std::printf("    %d picks heard\n", finder.picks());
        return static_cast<size_t>(finder.latency());
    };
}

PolyScore polyScore(const PolyScene& scene, const std::vector<Notes>& out, size_t latency, bool debug)
{
    PolyScore s;
    const size_t length = scene.truth.size();
    auto played = [&](size_t t) { return t + latency < out.size() ? out[t + latency] : Notes {}; };
    const auto run = static_cast<size_t>(0.005 * guitar::kRate);
    std::vector<double> delays;
    for (const guitar::Note& note : scene.notes)
    {
        const auto band = static_cast<size_t>(static_cast<int>(note.midi) - PolyNoteFinder::kFirstNote);
        const auto from = static_cast<size_t>(note.start * guitar::kRate);
        const auto to = std::min(length, static_cast<size_t>((note.start + guitar::truthLength(note)) * guitar::kRate));
        ++s.notes;
        size_t streak = 0;
        bool found = false;
        for (size_t t = from; t < to; ++t)
        {
            streak = played(t)[band] ? streak + 1 : 0;
            if (streak >= run)
            {
                found = true;
                delays.push_back(1000.0 * static_cast<double>(t + 1 - run - from) / guitar::kRate);
                size_t gap = 0;
                for (size_t u = t; u < to; ++u)
                {
                    if (played(u)[band])
                    {
                        if (gap > 0 && debug)
                            std::printf("    dropout %d at %.3f s for %.1f ms\n", static_cast<int>(note.midi),
                                        (u - gap) / guitar::kRate, 1000.0 * gap / guitar::kRate);
                        gap = 0;
                        continue;
                    }
                    ++gap;
                    s.dropoutMs += 1000.0 / guitar::kRate;
                }
                if (gap > 0 && debug)
                    std::printf("    dropout %d at %.3f s for %.1f ms (to the end)\n", static_cast<int>(note.midi),
                                (to - gap) / guitar::kRate, 1000.0 * gap / guitar::kRate);
                break;
            }
        }
        if (!found)
        {
            ++s.missed;
            if (debug)
                std::printf("    missed %d at %.3f s\n", static_cast<int>(note.midi), note.start);
        }
    }
    if (!delays.empty())
    {
        std::sort(delays.begin(), delays.end());
        s.medianDelayMs = delays[delays.size() / 2];
    }
    // extra notes: per band, stretches of 3 ms or more (gaps under 5 ms joined)
    const auto minimum = static_cast<size_t>(0.003 * guitar::kRate);
    const auto join = static_cast<size_t>(0.005 * guitar::kRate);
    for (size_t b = 0; b < static_cast<size_t>(PolyNoteFinder::kBands); ++b)
    {
        size_t begin = 0, end = 0;
        bool open = false;
        auto close = [&]() {
            if (open && end - begin >= minimum)
            {
                ++s.extra;
                if (debug)
                    std::printf("    extra %zu at %.3f s for %.1f ms\n", b + PolyNoteFinder::kFirstNote,
                                begin / guitar::kRate, 1000.0 * static_cast<double>(end - begin) / guitar::kRate);
            }
        };
        for (size_t t = 0; t < length; ++t)
        {
            const bool bad = played(t)[b] && !scene.truth[t][b] && !scene.dontCare[t][b];
            if (!bad)
                continue;
            s.extraMs += 1000.0 / guitar::kRate;
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

std::vector<PolyScore> polyBench(const char* name, const PolyTracker& tracker, bool print = true)
{
    const bool debug = std::getenv("NAMSAYIN_TRACKING_DEBUG") != nullptr;
    if (print)
    {
        std::printf("\n%s\n", name);
        std::printf("  %-18s %5s %6s %9s %6s %9s %8s\n", "scene", "notes", "missed", "delay ms", "extra", "extra ms",
                    "drop ms");
    }
    std::vector<PolyScore> scores;
    PolyScore total;
    for (const PolyScene& scene : polyScenes())
    {
        std::vector<Notes> out;
        const size_t latency = tracker(scene.performance.signal, out);
        const PolyScore s = polyScore(scene, out, latency, debug && print);
        if (print)
            std::printf("  %-18s %5d %6d %9.1f %6d %9.0f %8.0f\n", scene.name, s.notes, s.missed, s.medianDelayMs,
                        s.extra, s.extraMs, s.dropoutMs);
        scores.push_back(s);
        total.notes += s.notes;
        total.missed += s.missed;
        total.extra += s.extra;
        total.extraMs += s.extraMs;
        total.dropoutMs += s.dropoutMs;
    }
    if (print)
        std::printf("  %-18s %5d %6d %9s %6d %9.0f %8.0f\n", "total", total.notes, total.missed, "", total.extra,
                    total.extraMs, total.dropoutMs);
    scores.push_back(total);
    return scores;
}

} // namespace

TEST_CASE("poly tracking benchmark" * doctest::skip(std::getenv("NAMSAYIN_TRACKING_BENCH") == nullptr))
{
    polyBench("poly note finder", finderTracker(0.0));
    polyBench("poly note finder, lookahead 20 ms", finderTracker(20.0));
}

TEST_CASE("the poly note finder plays chords with few extra notes and no flicker")
{
    // the scores before the 2026-10-24 changes (no level follower, mute or re-pick rules, no hold in the peak test):
    // 45 missed, 151 extra stretches (3.6 s), 2.2 s of dropouts; with 20 ms lookahead 196 extra (9.9 s). Then 45
    // missed, 143 extra (2.7 s), 1.3 s of dropouts. The owner's recordings (RealTrackingTests.cpp) led to finding the
    // notes by their harmonics (2026-10-24): fewer missed and far fewer dropouts here, somewhat more extra time (the
    // synthetic guitar's strong fundamentals don't need it; real DI does), and on the recordings a third fewer strays.
    const auto plain = polyBench("poly note finder", finderTracker(0.0), false);
    CHECK(plain.back().missed <= 38);
    CHECK(plain.back().extra <= 140);
    CHECK(plain.back().extraMs <= 4500.0);
    CHECK(plain.back().dropoutMs <= 400.0);
    CHECK(plain[0].dropoutMs <= 300.0); // strummed chords: the thirds don't flicker
    const auto ahead = polyBench("poly note finder, lookahead 20 ms", finderTracker(20.0), false);
    CHECK(ahead.back().missed <= 21);
    CHECK(ahead.back().extraMs <= 6500.0);
    CHECK(ahead[2].medianDelayMs <= 10.0); // single notes start nearly on time
}
