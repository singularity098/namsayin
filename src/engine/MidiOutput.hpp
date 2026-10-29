// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <array>
#include <bitset>
#include <cstdint>

namespace xythos {

// One MIDI message on the way to the host, at an absolute sample time (samples since the engine was prepared)
struct MidiMessage {
    int64_t time = 0;
    uint8_t data[3] = { 0, 0, 0 };
    uint8_t size = 3;
};

// The notes each slot has sounding (16 channels x 128 notes), so the engine can end them when a block is bypassed,
// removed or rebuilt, and nothing hangs in the synth it drives
using MidiSounding = std::array<std::bitset<16 * 128>, 32>;

// What blocks write MIDI into during process() (Block::setMidiOutput()). Messages are timed by the frame in the
// block's buffer; the engine sets each node's base time (where the buffer starts, plus the latency of the blocks
// after it, so the MIDI lines up with the audio) and collects the messages afterwards. RT-safe: a fixed buffer,
// messages past its end are dropped.
class MidiOutput {
public:
    static constexpr int kCapacity = 512;

    // Engine: before a node's process()
    void setNode(int64_t baseTime, int slot, MidiSounding* sounding)
    {
        fBase = baseTime;
        fSlot = slot;
        fSounding = sounding;
    }

    void clear() { fCount = 0; }
    int count() const { return fCount; }
    const MidiMessage& at(int i) const { return fMessages[static_cast<size_t>(i)]; }

    // Blocks: channel 0..15, note and velocity 0..127 (a velocity under 1 is sent as 1)
    void noteOn(uint32_t frame, int channel, int note, int velocity)
    {
        if (!valid(channel, note))
            return;
        add(frame, static_cast<uint8_t>(0x90 | channel), static_cast<uint8_t>(note),
            static_cast<uint8_t>(velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity)));
        if (fSounding)
            (*fSounding)[static_cast<size_t>(fSlot)].set(static_cast<size_t>(channel * 128 + note));
    }

    void noteOff(uint32_t frame, int channel, int note)
    {
        if (!valid(channel, note))
            return;
        add(frame, static_cast<uint8_t>(0x80 | channel), static_cast<uint8_t>(note), 0);
        if (fSounding)
            (*fSounding)[static_cast<size_t>(fSlot)].reset(static_cast<size_t>(channel * 128 + note));
    }

    // value 0..16383, 8192 = no bend
    void pitchBend(uint32_t frame, int channel, int value)
    {
        if (channel < 0 || channel > 15)
            return;
        const int v = value < 0 ? 0 : (value > 16383 ? 16383 : value);
        add(frame, static_cast<uint8_t>(0xe0 | channel), static_cast<uint8_t>(v & 0x7f),
            static_cast<uint8_t>((v >> 7) & 0x7f));
    }

    // Engine: note-offs for everything a slot has sounding, and the bend reset on those channels
    void endSlot(int slot, int64_t time)
    {
        if (!fSounding)
            return;
        auto& notes = (*fSounding)[static_cast<size_t>(slot)];
        if (notes.none())
            return;
        const int64_t base = fBase;
        fBase = time;
        for (int channel = 0; channel < 16; ++channel)
        {
            bool used = false;
            for (int note = 0; note < 128; ++note)
            {
                if (!notes.test(static_cast<size_t>(channel * 128 + note)))
                    continue;
                add(0, static_cast<uint8_t>(0x80 | channel), static_cast<uint8_t>(note), 0);
                used = true;
            }
            if (used)
                add(0, static_cast<uint8_t>(0xe0 | channel), 0, 64);
        }
        notes.reset();
        fBase = base;
    }

private:
    static bool valid(int channel, int note) { return channel >= 0 && channel < 16 && note >= 0 && note < 128; }

    void add(uint32_t frame, uint8_t a, uint8_t b, uint8_t c)
    {
        if (fCount >= kCapacity)
            return;
        MidiMessage& m = fMessages[static_cast<size_t>(fCount++)];
        m.time = fBase + frame;
        m.data[0] = a;
        m.data[1] = b;
        m.data[2] = c;
        m.size = 3;
    }

    std::array<MidiMessage, kCapacity> fMessages {};
    int fCount = 0;
    int64_t fBase = 0;
    int fSlot = 0;
    MidiSounding* fSounding = nullptr;
};

} // namespace xythos
