// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/metronome/Metronome.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace namsayin {

namespace {

constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 300.0;
constexpr int64_t kEventsPerBeat = 16; // event IDs in sync mode: beat * kEventsPerBeat + subdivision

// The Classic sound (Fretbored's click): a tone with its 2nd and 3rd harmonics, 0.5 : 0.15 : 0.05, starting at full
// level and decaying with a 50 ms time constant, cut off after 50 ms; 0.5 ms and 6 ms fades soften the start and
// keep the cut from popping. The owner raised its pitch by two semitones (2026-10-26): 1500 and 1000 Hz were Fretbored's.
constexpr double kClassicAccentHz = 1500.0 * 1.122462048309373; // 2^(2/12)
constexpr double kClassicBeatHz = 1000.0 * 1.122462048309373;

std::vector<float> classicClick(double frequency, double rate)
{
    std::vector<float> out(static_cast<size_t>(rate * 0.05));
    const double fadeIn = rate * 0.0005;
    const size_t fadeOut = static_cast<size_t>(rate * 0.006);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = i / rate;
        const double w = 2.0 * M_PI * frequency * t;
        const double tone = 0.5 * std::sin(w) + 0.15 * std::sin(2.0 * w) + 0.05 * std::sin(3.0 * w);
        const double fade = std::min({ 1.0, i / fadeIn, static_cast<double>(out.size() - i) / fadeOut });
        out[i] = static_cast<float>(tone * std::exp(-t / 0.05) * fade);
    }
    return out;
}

std::vector<float> beep(double frequency, double seconds, double rate)
{
    std::vector<float> out(static_cast<size_t>(rate * seconds));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = i / rate;
        const double envelope = std::min(1.0, i / 40.0) * std::exp(-t * 60.0);
        out[i] = static_cast<float>(std::sin(2.0 * M_PI * frequency * t) * envelope * 0.9);
    }
    return out;
}

// The Digital sound: a short band-limited square wave (odd harmonics up to near Nyquist), held briefly, then a
// fast decay
std::vector<float> digitalClick(double frequency, double rate)
{
    std::vector<float> out(static_cast<size_t>(rate * 0.06));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = i / rate;
        double square = 0.0;
        for (int k = 1; k * frequency < 0.45 * rate; k += 2)
            square += std::sin(2.0 * M_PI * k * frequency * t) / k;
        const double envelope = std::min(1.0, i / 20.0) * (t < 0.02 ? 1.0 : std::exp(-(t - 0.02) * 120.0));
        out[i] = static_cast<float>(square * (4.0 / M_PI) * envelope * 0.45);
    }
    return out;
}

} // namespace

const Metronome::Offsets& Metronome::subdivisionOffsets(MetroSubdivision subdivision)
{
    // (constant: a table of vectors was built on the first click, allocating on the audio thread)
    static constexpr Offsets offsets[] = {
        { { 0.0 }, 1 },
        { { 0.0, 1.0 / 2 }, 2 },
        { { 0.0, 1.0 / 3, 2.0 / 3 }, 3 },
        { { 0.0, 1.0 / 4, 2.0 / 4, 3.0 / 4 }, 4 },
        { { 0.0, 1.0 / 5, 2.0 / 5, 3.0 / 5, 4.0 / 5 }, 5 },
        { { 0.0, 1.0 / 6, 2.0 / 6, 3.0 / 6, 4.0 / 6, 5.0 / 6 }, 6 },
        { { 0.0, 2.0 / 3 }, 2 },
        { { 0.0, 3.0 / 4 }, 2 },
        { { 0.0, 1.0 / 2, 3.0 / 4 }, 3 },
        { { 0.0, 1.0 / 4, 1.0 / 2 }, 3 },
        { { 0.0, 1.0 / 4, 3.0 / 4 }, 3 },
    };
    const int index = std::clamp(static_cast<int>(subdivision), 0, static_cast<int>(MetroSubdivision::Count) - 1);
    return offsets[index];
}

void Metronome::prepare(double sampleRate)
{
    fSampleRate = sampleRate;
    const int classic = static_cast<int>(MetroSound::Classic);
    const int digital = static_cast<int>(MetroSound::Digital);
    const int beepSound = static_cast<int>(MetroSound::Beep);
    fSamples[classic][kAccent] = classicClick(kClassicAccentHz, sampleRate);
    fSamples[classic][kBeat] = classicClick(kClassicBeatHz, sampleRate);
    fSamples[classic][kSub] = fSamples[classic][kBeat];
    fSamples[digital][kAccent] = digitalClick(1975.5, sampleRate); // B6
    fSamples[digital][kBeat] = digitalClick(1318.5, sampleRate);   // E6
    fSamples[digital][kSub] = fSamples[digital][kBeat];
    fSamples[beepSound][kAccent] = beep(1760.0, 0.06, sampleRate);
    fSamples[beepSound][kBeat] = beep(1100.0, 0.06, sampleRate);
    fSamples[beepSound][kSub] = beep(880.0, 0.05, sampleRate);

    for (Voice& voice : fVoices)
        voice = Voice {};
    fStatus = Status {};
    fRunning = false;
    fSyncWasPlaying = false;
}

void Metronome::process(float* out, uint32_t frames, const Settings& settings, const Transport* transport,
                        uint32_t offset)
{
    if (settings.sync)
    {
        fRunning = false;
        const bool playing = settings.on && transport && transport->valid && transport->playing &&
                             transport->bpm > 0.0;
        if (playing)
        {
            processSync(out, frames, settings, *transport, offset);
            return;
        }
        fSyncWasPlaying = false;
        fStatus.beat = -1;
        render(out, 0, frames);
        return;
    }
    fSyncWasPlaying = false;
    processFree(out, frames, settings);
}

void Metronome::start(const Settings& settings)
{
    fRunning = true;
    fTime = 0;
    fNextBeat = 0.0;
    fNextSub = std::numeric_limits<size_t>::max(); // no beat played yet
    fBeat = 0;
    fBar = 0;
    fCount = 0;
    fRampFrom = 0;
    fBarsSinceStep = 0;
    fTrainer = settings.trainer;
    fLiveBpm = std::clamp(fTrainer != MetroTrainer::Off ? settings.trainerStart : settings.bpm, kMinBpm, kMaxBpm);
    fStatus.bar = 0;
}

void Metronome::processFree(float* out, uint32_t frames, const Settings& settings)
{
    if (!settings.on)
    {
        // clicks already sounding play out (cutting them would pop)
        fRunning = false;
        fStatus.beat = -1;
        render(out, 0, frames);
        return;
    }
    if (!fRunning)
        start(settings);

    // Changing the trainer mode while running restarts the trainer from the next beat, without stopping
    if (settings.trainer != fTrainer)
    {
        fTrainer = settings.trainer;
        fBarsSinceStep = 0;
        fRampFrom = fCount;
        fLiveBpm = std::clamp(fTrainer != MetroTrainer::Off ? settings.trainerStart : settings.bpm, kMinBpm, kMaxBpm);
    }

    const Offsets& offsets = subdivisionOffsets(settings.subdivision);
    uint32_t cursor = 0;
    for (;;)
    {
        const bool sub = fNextSub < offsets.size();
        const double next = sub ? fBeatStart + offsets[fNextSub] * fBeatLength : fNextBeat;
        const int64_t at = std::llround(next) - fTime;
        if (at >= static_cast<int64_t>(frames))
            break;
        const auto position = static_cast<uint32_t>(std::max<int64_t>(at, cursor));
        render(out, cursor, position);
        cursor = position;

        if (sub)
        {
            subdivision(settings);
            ++fNextSub;
            continue;
        }
        fBeatStart = fNextBeat;
        fBeatLength = 60.0 / fLiveBpm * fSampleRate;
        fNextBeat = fBeatStart + fBeatLength;
        fNextSub = 1;
        beat(settings, fBeat, fBar, fLiveBpm);
        advance(settings);
    }
    render(out, cursor, frames);
    fTime += frames;
}

void Metronome::processSync(float* out, uint32_t frames, const Settings& settings, const Transport& transport,
                            uint32_t offset)
{
    const double rate = transport.bpm / 60.0 / fSampleRate; // beats per sample
    const double start = transport.beat + offset * rate;
    const double end = start + frames * rate;

    // Started, or the host jumped (loop, locate): forget which clicks were played
    if (!fSyncWasPlaying || start < fLastSyncBeat - rate || start > fLastSyncBeat + 2.0 * rate + 1e-9)
        fLastSyncEvent = std::numeric_limits<int64_t>::min();
    fSyncWasPlaying = true;
    fLastSyncBeat = end;

    const Offsets& offsets = subdivisionOffsets(settings.subdivision);
    const int beatsPerBar = std::clamp(transport.beatsPerBar, 1, kMaxBeats);
    uint32_t cursor = 0;
    // From the beat before, as its subdivisions may fall in this buffer. A click just before `start` (by less
    // than a sample, from rounding in the host's position) is played at the start rather than missed.
    for (auto n = static_cast<int64_t>(std::floor(start)) - 1; n < end; ++n)
    {
        for (size_t j = 0; j < offsets.size(); ++j)
        {
            const double position = n + offsets[j];
            const int64_t id = n * kEventsPerBeat + static_cast<int64_t>(j);
            if (position < start - rate || position >= end || id <= fLastSyncEvent)
                continue;
            const auto at = static_cast<uint32_t>(
                std::clamp<int64_t>(std::llround((position - start) / rate), cursor, frames - 1));
            render(out, cursor, at);
            cursor = at;
            fLastSyncEvent = id;
            if (j == 0)
            {
                const int beatInBar = static_cast<int>(((n % beatsPerBar) + beatsPerBar) % beatsPerBar);
                const auto bar = static_cast<uint32_t>(n < 0 ? 0 : n / beatsPerBar);
                beat(settings, beatInBar, bar, transport.bpm);
            }
            else
            {
                subdivision(settings);
            }
        }
    }
    render(out, cursor, frames);
}

bool Metronome::gapSilent(const Settings& settings, uint32_t bar) const
{
    const int play = std::max(1, settings.gapPlay);
    const int cycle = play + std::max(0, settings.gapMute);
    return settings.gap && settings.gapMute > 0 && static_cast<int>(bar % static_cast<uint32_t>(cycle)) >= play;
}

void Metronome::beat(const Settings& settings, int beatInBar, uint32_t bar, double bpm)
{
    fBeatSilent = gapSilent(settings, bar);
    const int level = beatInBar < kMaxBeats ? std::clamp(settings.levels[beatInBar], 0, 3) : 2;
    if (!fBeatSilent && level > 0)
    {
        const auto& set = fSamples[static_cast<int>(settings.sound)];
        trigger(set[level == 3 ? kAccent : kBeat], kLevelGain[level] * settings.volume);
    }
    fStatus.beat = beatInBar;
    fStatus.beatCount = (fStatus.beatCount + 1) & 0xffffffu;
    fStatus.bar = bar;
    fStatus.bpm = bpm;
    fStatus.silent = fBeatSilent;
}

void Metronome::subdivision(const Settings& settings)
{
    if (!fBeatSilent)
        trigger(fSamples[static_cast<int>(settings.sound)][kSub],
                settings.subVolume * settings.volume * kSubdivisionGain);
}

// Moves to the next beat, and lets the trainer change the tempo (as Fretbored does)
void Metronome::advance(const Settings& settings)
{
    ++fCount;
    if (++fBeat >= std::max(1, settings.beats))
    {
        fBeat = 0;
        ++fBar;
        if (fTrainer == MetroTrainer::Step && ++fBarsSinceStep >= std::max(1, settings.trainerEvery))
        {
            fBarsSinceStep = 0;
            const double step = std::fabs(settings.trainerStep);
            if (settings.trainerEnd > fLiveBpm)
                fLiveBpm = std::min(settings.trainerEnd, fLiveBpm + step);
            else if (settings.trainerEnd < fLiveBpm)
                fLiveBpm = std::max(settings.trainerEnd, fLiveBpm - step);
        }
    }
    if (fTrainer == MetroTrainer::Gradual)
    {
        const double total = static_cast<double>(std::max(1, settings.trainerBars) * std::max(1, settings.beats));
        const double progress = std::min(1.0, static_cast<double>(fCount - fRampFrom) / total);
        fLiveBpm = settings.trainerStart + (settings.trainerEnd - settings.trainerStart) * progress;
    }
    else if (fTrainer == MetroTrainer::Off)
    {
        fLiveBpm = settings.bpm;
    }
    fLiveBpm = std::clamp(fLiveBpm, kMinBpm, kMaxBpm);
}

void Metronome::trigger(const std::vector<float>& sample, float gain)
{
    if (gain <= 0.0f || sample.empty())
        return;
    // a free voice, or else the one that has played longest
    Voice* voice = &fVoices[0];
    for (Voice& v : fVoices)
    {
        if (!v.data)
        {
            voice = &v;
            break;
        }
        if (v.pos > voice->pos)
            voice = &v;
    }
    *voice = { sample.data(), static_cast<uint32_t>(sample.size()), 0, gain };
}

void Metronome::render(float* out, uint32_t from, uint32_t to)
{
    for (Voice& voice : fVoices)
    {
        if (!voice.data)
            continue;
        const uint32_t n = std::min(to - from, voice.length - voice.pos);
        for (uint32_t i = 0; i < n; ++i)
            out[from + i] += voice.data[voice.pos + i] * voice.gain;
        voice.pos += n;
        if (voice.pos >= voice.length)
            voice.data = nullptr;
    }
}

} // namespace namsayin
