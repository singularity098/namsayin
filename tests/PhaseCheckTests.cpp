// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "engine/PhaseCheck.hpp"
#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/NamBlock.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace xythos;

namespace {

// A mono 32-bit float WAV at 48 kHz
std::string writeIr(const std::string& name, const std::vector<float>& x)
{
    const std::string path = (std::filesystem::temp_directory_path() / ("xythos-phase-" + name)).string();
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t size = static_cast<uint32_t>(x.size() * 4);
    out.write("RIFF", 4);
    u32(36 + size);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(1);
    u32(48000);
    u32(48000 * 4);
    u16(4);
    u16(32);
    out.write("data", 4);
    u32(size);
    out.write(reinterpret_cast<const char*>(x.data()), static_cast<std::streamsize>(size));
    return path;
}

// Something like a cab: a decaying, resonant response starting `delay` samples in, with polarity `sign`
std::vector<float> cab(float sign, size_t delay, double freq)
{
    std::vector<float> x(2048, 0.0f);
    for (size_t i = delay; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i - delay) / 48000.0;
        // a spike that carries the low end, as a real cab IR's first peak does, and a decaying resonance
        x[i] = sign * static_cast<float>(std::exp(-t * 1500.0) + 0.6 * std::exp(-t * 400.0) * std::cos(2.0 * M_PI * freq * t));
    }
    return x;
}

PhaseBlock block(const BlockDescriptor& type, const std::string& path)
{
    PhaseBlock b;
    b.type = &type;
    b.config = { { "path", path } };
    if (&type == &kNamBlockDescriptor)
        b.config[kNamToneKey] = true;
    for (int p = 0; p < type.numParams; ++p)
        b.params[static_cast<size_t>(p)] = type.params[static_cast<size_t>(p)].toNormalized(type.params[static_cast<size_t>(p)].def);
    return b;
}

} // namespace

TEST_CASE("phase check: two cab IRs of opposite polarity, and of the same, a little apart in time")
{
    const std::string a = writeIr("a.wav", cab(1.0f, 0, 900.0));
    const std::string flipped = writeIr("flipped.wav", cab(-1.0f, 0, 1100.0));
    const std::string later = writeIr("later.wav", cab(1.0f, 30, 700.0));

    const PhaseResult opposite = compareBranchPolarity({ block(kIrBlockDescriptor, a) }, { block(kIrBlockDescriptor, flipped) }, 0.0);
    CAPTURE(opposite.correlation);
    CHECK(opposite.polarity == Polarity::Opposite);
    const PhaseResult same = compareBranchPolarity({ block(kIrBlockDescriptor, a) }, { block(kIrBlockDescriptor, later) }, 0.0);
    CAPTURE(same.correlation);
    CHECK(same.polarity == Polarity::Same);
    CHECK(std::abs(same.lag) <= 24);
    // Align delaying B moves where it looks
    const PhaseResult aligned = compareBranchPolarity({ block(kIrBlockDescriptor, later) }, { block(kIrBlockDescriptor, a) }, 30.0 / 48.0);
    CHECK(aligned.polarity == Polarity::Same);
}

TEST_CASE("phase check: the same NAM model on both sides is in phase; nothing to compare is Unknown")
{
    const std::string model = std::string(XYTHOS_EXAMPLE_MODELS) + "/wavenet.nam";
    const std::string ir = writeIr("cab.wav", cab(1.0f, 0, 900.0));
    const std::string flipped = writeIr("cab-flipped.wav", cab(-1.0f, 0, 900.0));
    CHECK(compareBranchPolarity({ block(kNamBlockDescriptor, model) }, { block(kNamBlockDescriptor, model) }, 0.0).polarity
          == Polarity::Same);
    CHECK(compareBranchPolarity({ block(kNamBlockDescriptor, model), block(kIrBlockDescriptor, ir) },
                                { block(kNamBlockDescriptor, model), block(kIrBlockDescriptor, flipped) }, 0.0).polarity
          == Polarity::Opposite);
    CHECK(compareBranchPolarity({}, { block(kIrBlockDescriptor, ir) }, 0.0).polarity == Polarity::Unknown);
    CHECK(compareBranchPolarity({ block(kIrBlockDescriptor, "/no/such/file.wav") }, { block(kIrBlockDescriptor, ir) }, 0.0).polarity
          == Polarity::Unknown);
}
