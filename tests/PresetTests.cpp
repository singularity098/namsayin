// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/Params.hpp"
#include "state/Library.hpp"
#include "state/Paths.hpp"
#include "state/Preset.hpp"
#include "state/UserImages.hpp"

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
        testing::setEnv("NAMSAYIN_CONFIG_DIR", pathToUtf8(root / "config"));
        model = pathToUtf8(root / "lib" / "Amp.nam");
        ir = pathToUtf8(root / "lib" / "Cab.wav");
        std::ofstream(pathFromUtf8(model), std::ios::binary) << "model bytes";
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

    SUBCASE("embedded copies are extracted")
    {
        const std::optional<LoadedPreset> loaded = loadPresetFile(embedded, nullptr);
        REQUIRE(loaded);
        CHECK(loaded->sources[0] == FileSource::Embedded);
        CHECK(loaded->sources[1] == FileSource::Embedded);
        const std::string path = loaded->preset.chain.blocks[0].config["path"];
        CHECK(readFile(pathFromUtf8(path)) == "model bytes");
        CHECK(fs::path(pathFromUtf8(path)).filename() == "Amp.nam");
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

TEST_CASE("preset folder: paths from folder and name, listing, inside checks")
{
    const fs::path dir = fs::temp_directory_path() / "namsayin-test-preset-folder";
    fs::remove_all(dir);

    CHECK(presetPath(dir, "", "Clean") == dir / "Clean.namsayin");
    CHECK(presetPath(dir, " Amps / Fender ", " Twin ") == dir / "Amps" / "Fender" / "Twin.namsayin");
    CHECK(presetPath(dir, "Amps\\Fender", "Twin.namsayin") == dir / "Amps" / "Fender" / "Twin.namsayin");
    CHECK(presetPath(dir, "//", "Ünïcode") == dir / pathFromUtf8("Ünïcode.namsayin"));
    CHECK_FALSE(presetPath(dir, "", "  "));
    CHECK_FALSE(presetPath(dir, "", "a/b"));
    CHECK_FALSE(presetPath(dir, "", "what?"));
    CHECK_FALSE(presetPath(dir, "..", "Escape"));
    CHECK_FALSE(presetPath(dir, "Amps/../..", "Escape"));

    Preset preset;
    for (const auto& [folder, name] : { std::pair { "", "zeta" }, { "", "Alpha" }, { "Clean", "Twin" },
                                        { "Clean/Fender", "Deluxe" }, { "Lead", "Plexi" } })
        REQUIRE(savePresetFile(pathToUtf8(*presetPath(dir, folder, name)), preset, false));
    fs::create_directories(dir / "Empty");
    std::ofstream(dir / "notes.txt") << "not a preset";

    const std::vector<PresetEntry> presets = listPresets(dir);
    REQUIRE(presets.size() == 5);
    CHECK(presets[0].name == "Alpha");
    CHECK(presets[0].folder == "");
    CHECK(presets[1].name == "zeta");
    CHECK(presets[2].folder == "Clean");
    CHECK(presets[3].folder == "Clean/Fender");
    CHECK(presets[3].name == "Deluxe");
    CHECK(presets[4].folder == "Lead");
    CHECK(pathFromUtf8(presets[4].path) == dir / "Lead" / "Plexi.namsayin");

    CHECK(listPresetFolders(dir) == std::vector<std::string> { "Clean", "Clean/Fender", "Empty", "Lead" });
    CHECK(listPresets(dir / "missing").empty());

    CHECK(isInsideDir(dir, dir / "Clean" / "Twin.namsayin"));
    CHECK(isInsideDir(dir, dir / "New" / "Not yet.namsayin"));
    CHECK_FALSE(isInsideDir(dir, dir));
    CHECK_FALSE(isInsideDir(dir, dir.parent_path() / "elsewhere.namsayin"));
    CHECK_FALSE(isInsideDir(dir, dir / ".." / "elsewhere.namsayin"));
}

TEST_CASE("deleting a preset: only preset files inside the preset folder")
{
    const fs::path root = fs::temp_directory_path() / "namsayin-test-preset-delete";
    fs::remove_all(root);
    const fs::path dir = root / "presets";
    Preset preset;
    const std::string twin = pathToUtf8(*presetPath(dir, "Clean", "Twin"));
    const std::string outside = pathToUtf8(root / "Outside.namsayin");
    REQUIRE(savePresetFile(twin, preset, false));
    REQUIRE(savePresetFile(outside, preset, false));
    std::ofstream(dir / "notes.txt") << "not a preset";

    CHECK(deletePreset(dir, twin));
    CHECK_FALSE(fs::exists(pathFromUtf8(twin)));
    CHECK(listPresets(dir).empty());
    CHECK_FALSE(deletePreset(dir, twin)); // already gone

    CHECK_FALSE(deletePreset(dir, outside));
    CHECK(fs::exists(pathFromUtf8(outside)));
    CHECK_FALSE(deletePreset(dir, pathToUtf8(dir / "notes.txt")));
    CHECK(fs::exists(dir / "notes.txt"));
    CHECK_FALSE(deletePreset(dir, pathToUtf8(dir / "Clean"))); // a folder
    CHECK(fs::exists(dir / "Clean"));
    fs::remove_all(root);
}

TEST_CASE("exported presets carry the user's block images, which go into the library when loaded")
{
    PresetFixture fixture;
    const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + std::string(16, '\0') + "\x08 amp image";
    const std::string jpeg = "\xff\xd8\xff pedal image";
    REQUIRE(addUserImage(ImageKind::Amp, "My Amp.png", png) == "My Amp.png");
    REQUIRE(addUserImage(ImageKind::Pedal, "Delay.jpg", jpeg) == "Delay.jpg");
    Preset preset = fixture.preset();
    preset.chain.blocks[0].image = "My Amp.png";
    preset.chain.blocks[2].image = "Delay.jpg";
    const std::string embedded = pathToUtf8(fixture.root / "Embedded.namsayin");
    const std::string plain = pathToUtf8(fixture.root / "Plain.namsayin");
    REQUIRE(savePresetFile(embedded, preset, true));
    REQUIRE(savePresetFile(plain, preset, false));
    CHECK(readLibraryFile(embedded + "!/images/amps/My Amp.png") == png);
    CHECK(readLibraryFile(embedded + "!/images/pedals/Delay.jpg") == jpeg);
    CHECK_FALSE(readLibraryFile(plain + "!/images/amps/My Amp.png"));

    SUBCASE("the library has them: they're used as they are")
    {
        const std::optional<LoadedPreset> loaded = loadPresetFile(embedded, nullptr);
        REQUIRE(loaded);
        CHECK(loaded->preset.chain.blocks[0].image == "My Amp.png");
        CHECK(listUserImages(ImageKind::Amp).size() == 1);
    }

    SUBCASE("deleted: an embedded one comes back, a saved preset's block keeps the id and shows the stock image")
    {
        REQUIRE(deleteUserImage(ImageKind::Amp, "My Amp.png"));
        const std::optional<LoadedPreset> fromPlain = loadPresetFile(plain, nullptr);
        REQUIRE(fromPlain);
        CHECK(fromPlain->preset.chain.blocks[0].image == "My Amp.png");
        CHECK(userImagePath(ImageKind::Amp, "My Amp.png").empty());

        const std::optional<LoadedPreset> loaded = loadPresetFile(embedded, nullptr);
        REQUIRE(loaded);
        CHECK(loaded->preset.chain.blocks[0].image == "My Amp.png");
        CHECK(readFile(pathFromUtf8(userImagePath(ImageKind::Amp, "My Amp.png"))) == png);
    }

    SUBCASE("another image has the name: the embedded one is added under a new name")
    {
        REQUIRE(deleteUserImage(ImageKind::Amp, "My Amp.png"));
        const std::string other = png + " other";
        REQUIRE(addUserImage(ImageKind::Amp, "My Amp.png", other) == "My Amp.png");
        const std::optional<LoadedPreset> loaded = loadPresetFile(embedded, nullptr);
        REQUIRE(loaded);
        CHECK(loaded->preset.chain.blocks[0].image == "My Amp 2.png");
        CHECK(readFile(pathFromUtf8(userImagePath(ImageKind::Amp, "My Amp 2.png"))) == png);
        CHECK(loaded->preset.chain.blocks[2].image == "Delay.jpg");
    }
}
