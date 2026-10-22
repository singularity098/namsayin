// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/metronome/Metronome.hpp"

#include "engine/metronome/ClickSamples.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace namsayin {

namespace {

constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 300.0;
constexpr int64_t kEventsPerBeat = 16; // event IDs in sync mode: beat * kEventsPerBeat + subdivision

// Resamples a click to `rate` and fades its ends, as some samples start or end on a non-zero value (the Classic
// clicks are cut off mid-decay), which pops: 0.5 ms in, 6 ms out (as Fretbored does)
std::vector<float> renderClick(const ClickSample& click, double rate)
{
    const double step = static_cast<double>(click.sampleRate) / rate;
    const auto length = static_cast<size_t>((click.length - 1) / step);
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        const double x = i * step;
        const auto j = static_cast<size_t>(x);
        const double frac = x - j;
        const double a = click.data[j], b = click.data[std::min<size_t>(j + 1, click.length - 1)];
        out[i] = static_cast<float>((a + (b - a) * frac) / 32768.0);
    }
    const auto fadeIn = static_cast<size_t>(rate * 0.0005);
    const size_t fadeOut = std::min(static_cast<size_t>(rate * 0.006), length / 3);
    for (size_t i = 0; i < fadeIn && i < length; ++i)
        out[i] *= static_cast<float>(i) / fadeIn;
    for (size_t i = 0; i < fadeOut; ++i)
        out[length - 1 - i] *= static_cast<float>(i) / fadeOut;
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

const std::vector<double>& Metronome::subdivisionOffsets(MetroSubdivision subdivision)
{
    static const std::vector<double> offsets[] = {
        { 0.0 },
        { 0.0, 1.0 / 2 },
        { 0.0, 1.0 / 3, 2.0 / 3 },
        { 0.0, 1.0 / 4, 2.0 / 4, 3.0 / 4 },
        { 0.0, 1.0 / 5, 2.0 / 5, 3.0 / 5, 4.0 / 5 },
        { 0.0, 1.0 / 6, 2.0 / 6, 3.0 / 6, 4.0 / 6, 5.0 / 6 },
        { 0.0, 2.0 / 3 },
        { 0.0, 3.0 / 4 },
        { 0.0, 1.0 / 2, 3.0 / 4 },
        { 0.0, 1.0 / 4, 1.0 / 2 },
        { 0.0, 1.0 / 4, 3.0 / 4 },
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
    fSamples[classic][kAccent] = renderClick(kClassicHigh, sampleRate);
    fSamples[classic][kBeat] = renderClick(kClassicLow, sampleRate);
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

    const std::vector<double>& offsets = subdivisionOffsets(settings.subdivision);
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

    const std::vector<double>& offsets = subdivisionOffsets(settings.subdivision);
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
