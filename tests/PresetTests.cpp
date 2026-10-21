// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/Params.hpp"
#include "state/Library.hpp"
#include "state/Paths.hpp"
#include "state/Preset.hpp"

#include <filesystem>
#include <fstream>

using namespace namsayin;
namespace fs = std::filesystem;

namespace {

struct PresetFixture {
    fs::path root = fs::temp_directory_path() / "namsayin-test-presets";
    std::string model, ir;

    PresetFixture()
    {
        fs::remove_all(root);
        fs::create_directories(root / "lib");
        testing::setEnv("NAMSAYIN_CACHE_DIR", pathToUtf8(root / "cache"));
        model = pathToUtf8(root / "lib" / "Amp.nam");
        ir = pathToUtf8(root / "lib" / "Cab.wav");
        std::ofstream(pathFromUtf8(model), std::ios::binary) << "model bytes";
        std::ofstream(root / "lib" / "Amp.png", std::ios::binary) << "image bytes";
        std::ofstream(pathFromUtf8(ir), std::ios::binary) << "ir bytes";
    }

    Preset preset() const
    {
        Preset p;
        BlockState amp { "nam", 0 };
        amp.config = { { "path", model }, { "a2Size", 1.0 } };
        amp.params[0] = 0.75f;
        BlockState cab { "ir", 1 };
        cab.config = { { "path", ir } };
        cab.bypass = true;
        p.chain.blocks = { amp, cab, BlockState { "delay", 2 } };
        p.chain.setMidiMapping({ 0, 7, slotParam(0, 0) });
        p.globals = { { "input", 3.0f }, { "bass", -2.0f } };
        return p;
    }
};

} // namespace

TEST_CASE("presets round-trip through JSON, without the preset's own path")
{
    PresetFixture fixture;
    Preset preset = fixture.preset();
    preset.chain.preset = "/somewhere/else.namsayin";
    const std::optional<Preset> back = Preset::fromJson(preset.toJson());
    REQUIRE(back);
    CHECK(back->chain.blocks == preset.chain.blocks);
    CHECK(back->chain.midi == preset.chain.midi);
    CHECK(back->chain.preset.empty());
    CHECK(back->globals == preset.globals);
    CHECK_FALSE(Preset::fromJson("{\"version\": 99, \"chain\": {}}"));
}

TEST_CASE("a saved preset loads with its files where they are, hashes added")
{
    PresetFixture fixture;
    const std::string file = pathToUtf8(fixture.root / "Clean.namsayin");
    REQUIRE(savePresetFile(file, fixture.preset(), false));

    const std::optional<LoadedPreset> loaded = loadPresetFile(file, nullptr);
    REQUIRE(loaded);
    CHECK(loaded->preset.chain.preset == file);
    REQUIRE(loaded->sources.size() == 3);
    CHECK(loaded->sources[0] == FileSource::Original);
    CHECK(loaded->preset.chain.blocks[0].config["path"] == fixture.model);
    CHECK(loaded->preset.chain.blocks[0].config["hash"] == hashBytes("model bytes"));
    CHECK(loaded->preset.chain.blocks[1].bypass);
    CHECK(loaded->preset.globals.at("input") == 3.0f);
}

TEST_CASE("missing files come from the embedded copies, then the library, else stay missing")
{
    PresetFixture fixture;
    const std::string embedded = pathToUtf8(fixture.root / "Embedded.namsayin");
    const std::string plain = pathToUtf8(fixture.root / "Plain.namsayin");
    REQUIRE(savePresetFile(embedded, fixture.preset(), true));
    REQUIRE(savePresetFile(plain, fixture.preset(), false));

    // the files disappear
    fs::remove_all(fixture.root / "lib");

    SUBCASE("embedded copies are extracted, with the sidecar image next to them")
    {
        const std::optional<LoadedPreset> loaded = loadPresetFile(embedded, nullptr);
        REQUIRE(loaded);
        CHECK(loaded->sources[0] == FileSource::Embedded);
        CHECK(loaded->sources[1] == FileSource::Embedded);
        const std::string path = loaded->preset.chain.blocks[0].config["path"];
        CHECK(readFile(pathFromUtf8(path)) == "model bytes");
        CHECK(fs::path(pathFromUtf8(path)).filename() == "Amp.nam");
        CHECK(readFile(pathFromUtf8(findSidecarImage(path))) == "image bytes");
    }

    SUBCASE("without embedded copies, a library file with the same content is used")
    {
        const std::string libraryCopy = "/library/Renamed Amp.nam";
        const std::optional<LoadedPreset> loaded = loadPresetFile(plain, [&](const std::string& hash) {
            return hash == hashBytes("model bytes") ? libraryCopy : std::string();
        });
        REQUIRE(loaded);
        CHECK(loaded->sources[0] == FileSource::Library);
        CHECK(loaded->preset.chain.blocks[0].config["path"] == libraryCopy);
        CHECK(loaded->sources[1] == FileSource::Missing);
        CHECK(loaded->preset.chain.blocks[1].config["path"] == fixture.ir); // kept, so the UI can show it
        CHECK(loaded->sources[2] == FileSource::Original);                  // no file
    }
}

TEST_CASE("embedding fails cleanly when a file can't be read; a non-preset file doesn't load")
{
    PresetFixture fixture;
    Preset preset = fixture.preset();
    preset.chain.blocks[0].config["path"] = pathToUtf8(fixture.root / "gone.nam");
    std::string error;
    CHECK_FALSE(savePresetFile(pathToUtf8(fixture.root / "x.namsayin"), preset, true, &error));
    CHECK(error.find("gone.nam") != std::string::npos);

    CHECK_FALSE(loadPresetFile(fixture.model, nullptr, &error));
}
