// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/Preset.hpp"

#include "engine/Params.hpp"
#include "state/Library.hpp"
#include "state/Paths.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <set>
#include <system_error>

namespace namsayin {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr const char* kPresetEntry = "preset.json";

bool fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
    return false;
}

std::string fileName(const std::string& path)
{
    if (const std::optional<ArchivePath> archive = splitArchivePath(path))
    {
        const size_t slash = archive->entry.find_last_of('/');
        return slash == std::string::npos ? archive->entry : archive->entry.substr(slash + 1);
    }
    return pathToUtf8(pathFromUtf8(path).filename());
}

bool exists(const std::string& path)
{
    std::error_code ec;
    const std::optional<ArchivePath> archive = splitArchivePath(path);
    if (!archive)
        return fs::is_regular_file(pathFromUtf8(path), ec);
    return readLibraryFile(path).has_value();
}

// Zip writing through miniz's heap writer, saved atomically at the end
class ZipWriter {
public:
    ZipWriter() { fOk = mz_zip_writer_init_heap(&fZip, 0, 0); }
    ~ZipWriter() { mz_zip_writer_end(&fZip); }

    bool add(const std::string& name, const std::string& data)
    {
        return fOk && mz_zip_writer_add_mem(&fZip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
    }

    bool save(const fs::path& path)
    {
        void* buffer = nullptr;
        size_t size = 0;
        if (!fOk || !mz_zip_writer_finalize_heap_archive(&fZip, &buffer, &size))
            return false;
        // the finished archive belongs to us now
        const std::string data(static_cast<const char*>(buffer), size);
        mz_free(buffer);
        return writeFileAtomic(path, data);
    }

private:
    mz_zip_archive fZip {};
    bool fOk = false;
};

} // namespace

const std::vector<uint32_t>& presetGlobalParams()
{
    static const std::vector<uint32_t> params = {
        kParamInputMode, kParamInput, kParamBass, kParamMiddle, kParamTreble, kParamOutput, kParamLimiter,
    };
    return params;
}

std::string Preset::toJson() const
{
    json root {
        { "version", kVersion },
        { "chain", json::parse(chain.toJson()) },
        { "globals", globals },
    };
    root["chain"].erase("preset"); // where a preset was loaded from isn't part of it
    return root.dump(2);
}

std::optional<Preset> Preset::fromJson(std::string_view text, std::string* error)
{
    try
    {
        const json root = json::parse(text);
        const int version = root.value("version", 0);
        if (version < 1 || version > kVersion)
        {
            fail(error, "unsupported preset version " + std::to_string(version));
            return std::nullopt;
        }
        std::optional<ChainState> chain = ChainState::fromJson(root.at("chain").dump(), error);
        if (!chain)
            return std::nullopt;
        Preset preset;
        preset.chain = std::move(*chain);
        preset.globals = root.value("globals", std::map<std::string, float>());
        return preset;
    }
    catch (const json::exception& e)
    {
        fail(error, e.what());
        return std::nullopt;
    }
}

bool savePresetFile(const std::string& path, const Preset& preset, bool embedFiles, std::string* error)
{
    Preset saved = preset;
    ZipWriter zip;

    std::set<std::string> written;
    for (BlockState& block : saved.chain.blocks)
    {
        const std::string file = block.config.value("path", std::string());
        if (file.empty())
            continue;
        // the content hash lets a missing file be found again (embedded copy or library match)
        const std::optional<std::string> bytes = readLibraryFile(file);
        if (!bytes)
        {
            if (embedFiles)
                return fail(error, "can't read " + file);
            continue;
        }
        const std::string hash = hashBytes(*bytes);
        block.config["hash"] = hash;
        if (!embedFiles || !written.insert(hash).second)
            continue;

        const std::string dir = "files/" + hash + "/";
        if (!zip.add(dir + fileName(file), *bytes))
            return fail(error, "can't write the preset");
        // the sidecar image goes in the same folder, so it's found next to the extracted file
        const std::string image = findSidecarImage(file);
        if (!image.empty())
        {
            if (const std::optional<std::string> imageBytes = readLibraryFile(image))
                zip.add(dir + fileName(image), *imageBytes);
        }
    }

    if (!zip.add(kPresetEntry, saved.toJson()))
        return fail(error, "can't write the preset");
    if (!zip.save(pathFromUtf8(path)))
        return fail(error, "can't save " + path);
    return true;
}

std::optional<LoadedPreset> loadPresetFile(const std::string& path,
                                           const std::function<std::string(const std::string& hash)>& findByHash,
                                           std::string* error)
{
    const std::string archive = path + std::string(kArchiveSeparator);
    const std::optional<std::string> text = readLibraryFile(archive + kPresetEntry);
    if (!text)
    {
        fail(error, "not a NAMsayin preset: " + path);
        return std::nullopt;
    }
    std::optional<Preset> preset = Preset::fromJson(*text, error);
    if (!preset)
        return std::nullopt;

    LoadedPreset loaded;
    for (BlockState& block : preset->chain.blocks)
    {
        const std::string file = block.config.value("path", std::string());
        const std::string hash = block.config.value("hash", std::string());
        FileSource source = FileSource::Original;
        if (!file.empty() && !exists(file))
        {
            source = FileSource::Missing;
            const std::string embedded = archive + "files/" + hash + "/" + fileName(file);
            if (!hash.empty() && readLibraryFile(embedded))
            {
                // extract next to its sidecar image, under the content hash so presets share copies
                const fs::path dir = cacheDir() / "presets" / hash;
                const fs::path target = dir / pathFromUtf8(fileName(file));
                std::error_code ec;
                if (!fs::exists(target, ec))
                {
                    writeFileAtomic(target, *readLibraryFile(embedded));
                    const std::string image = findSidecarImage(embedded);
                    if (!image.empty())
                    {
                        if (const std::optional<std::string> bytes = readLibraryFile(image))
                            writeFileAtomic(dir / pathFromUtf8(fileName(image)), *bytes);
                    }
                }
                if (fs::exists(target, ec))
                {
                    block.config["path"] = pathToUtf8(target);
                    source = FileSource::Embedded;
                }
            }
            if (source == FileSource::Missing && !hash.empty() && findByHash)
            {
                const std::string match = findByHash(hash);
                if (!match.empty())
                {
                    block.config["path"] = match;
                    source = FileSource::Library;
                }
            }
        }
        loaded.sources.push_back(source);
    }
    preset->chain.preset = path;
    loaded.preset = std::move(*preset);
    return loaded;
}

} // namespace namsayin
