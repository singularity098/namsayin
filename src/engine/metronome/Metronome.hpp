// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace namsayin {

enum class MetroSubdivision {
    Quarter,      // no subdivision
    Eighths,
    Triplets,
    Sixteenths,
    Quintuplets,
    Sextuplets,
    Swing,        // 0, 2/3
    Dotted,       // dotted 8th + 16th
    EightTwo16,   // 8th + two 16ths
    Two16Eight,   // two 16ths + 8th
    SixteenEightSixteen,
    Count
};

enum class MetroSound { Classic, Digital, Beep, Count };
enum class MetroTrainer { Off, Step, Gradual };

// The host's transport, for DAW sync. Positions are in the host's beats.
struct Transport {
    bool valid = false;   // the host gave a bar/beat position
    bool playing = false;
    double bpm = 120.0;
    double beat = 0.0;    // beats since the start of the song, at the start of the buffer
    int beatsPerBar = 4;
};

// The metronome (docs/Architecture.md 4.10), with the feature set of Fretbored's: meter and per-beat accents,
// subdivisions, three sounds, step and gradual tempo trainer, gap trainer. Clicks are scheduled to the exact
// sample: each beat's time is kept as a fractional sample position and rounded, so tempo never drifts.
//
// Free mode runs on its own tempo from when it's switched on. In sync mode it follows the host's tempo, meter and
// transport position, and only clicks while the transport plays.
class Metronome {
public:
    static constexpr int kMaxBeats = 16;
    static constexpr int kVoices = 8;
    static constexpr float kLevelGain[4] = { 0.0f, 0.4f, 0.8f, 1.0f }; // mute, soft, normal, accent
    static constexpr float kSubdivisionGain = 0.75f;

    struct Settings {
        bool on = false;
        bool sync = false;
        double bpm = 100.0;
        int beats = 4;
        MetroSubdivision subdivision = MetroSubdivision::Quarter;
        MetroSound sound = MetroSound::Classic;
        float volume = 0.8f;       // 0..1
        float subVolume = 0.5f;    // 0..1, relative to volume
        std::array<int, kMaxBeats> levels {}; // 0 mute .. 3 accent
        MetroTrainer trainer = MetroTrainer::Off;
        double trainerStart = 80.0, trainerEnd = 120.0, trainerStep = 5.0;
        int trainerEvery = 4, trainerBars = 32;
        bool gap = false;
        int gapPlay = 2, gapMute = 2;
    };

    // What's being heard, for the UI
    struct Status {
        int beat = -1;          // -1 when stopped
        uint32_t beatCount = 0; // increments on every beat
        uint32_t bar = 0;
        double bpm = 0.0;
        bool silent = false;    // the gap trainer is muting
    };

    // Onsets of each subdivision within a beat, as fractions of the beat (a fixed table: the audio thread reads it)
    struct Offsets {
        double values[6];
        size_t count;
        size_t size() const { return count; }
        double operator[](size_t i) const { return values[i]; }
    };
    static const Offsets& subdivisionOffsets(MetroSubdivision subdivision);

    // Non-RT. Renders the sounds at this rate.
    void prepare(double sampleRate);

    // RT. Adds the clicks for `frames` samples to `out`. `transport` may be null (no host position); its beat is
    // for the start of the host's buffer, and `offset` is where this call starts within it.
    void process(float* out, uint32_t frames, const Settings& settings, const Transport* transport = nullptr,
                 uint32_t offset = 0);

    const Status& status() const { return fStatus; }

private:
    struct Voice {
        const float* data = nullptr;
        uint32_t length = 0;
        uint32_t pos = 0;
        float gain = 0.0f;
    };

    enum Sample { kAccent, kBeat, kSub };

    void start(const Settings& settings);
    void beat(const Settings& settings, int beatInBar, uint32_t bar, double bpm);
    void subdivision(const Settings& settings);
    void advance(const Settings& settings);
    void trigger(const std::vector<float>& sample, float gain);
    void processFree(float* out, uint32_t frames, const Settings& settings);
    void processSync(float* out, uint32_t frames, const Settings& settings, const Transport& transport,
                     uint32_t offset);
    void render(float* out, uint32_t from, uint32_t to);
    bool gapSilent(const Settings& settings, uint32_t bar) const;

    double fSampleRate = 48000.0;
    std::vector<float> fSamples[static_cast<int>(MetroSound::Count)][3];
    Voice fVoices[kVoices];
    Status fStatus;

    // free mode
    bool fRunning = false;
    MetroTrainer fTrainer = MetroTrainer::Off;
    int64_t fTime = 0;       // samples since start, at the start of the current call
    double fBeatStart = 0.0; // of the beat being played
    double fBeatLength = 0.0;
    double fNextBeat = 0.0;
    size_t fNextSub = 1;     // next subdivision of the current beat
    int fBeat = 0;
    uint32_t fBar = 0;
    uint64_t fCount = 0;     // beats since start
    uint64_t fRampFrom = 0;
    int fBarsSinceStep = 0;
    double fLiveBpm = 100.0;
    bool fBeatSilent = false;

    // sync mode
    bool fSyncWasPlaying = false;
    int64_t fLastSyncEvent = -1; // beat * kMaxSubdivisions + subdivision of the last click
    double fLastSyncBeat = 0.0;
};

} // namespace namsayin
