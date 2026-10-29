// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/Library.hpp"

#include "state/Paths.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <system_error>

namespace namsayin {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {


std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool hasExtension(const std::string& name, std::string_view ext)
{
    return name.size() >= ext.size() && lower(name.substr(name.size() - ext.size())) == ext;
}

std::string_view extensionFor(LibraryKind kind)
{
    return kind == LibraryKind::Nam ? ".nam" : ".wav";
}

int64_t modificationTime(const fs::path& path)
{
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

// Name without extension, from a '/'-separated or native path
std::string stem(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

// A zip archive opened for reading. miniz opens files through stdio; on Windows we open them ourselves so
// non-ASCII paths work.
class Zip {
public:
    explicit Zip(const fs::path& path)
    {
#ifdef _WIN32
        if (_wfopen_s(&fFile, path.c_str(), L"rb") != 0)
            fFile = nullptr;
#else
        fFile = std::fopen(path.c_str(), "rb");
#endif
        if (fFile == nullptr)
            return;
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        fOpen = !ec && mz_zip_reader_init_cfile(&fZip, fFile, size, 0);
    }

    ~Zip()
    {
        if (fOpen)
            mz_zip_reader_end(&fZip);
        if (fFile)
            std::fclose(fFile);
    }

    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    bool isOpen() const { return fOpen; }
    mz_uint count() { return mz_zip_reader_get_num_files(&fZip); }

    bool stat(mz_uint index, mz_zip_archive_file_stat& st) { return mz_zip_reader_file_stat(&fZip, index, &st); }

    int find(const std::string& entry) { return mz_zip_reader_locate_file(&fZip, entry.c_str(), nullptr, 0); }

    std::optional<std::string> read(mz_uint index)
    {
        mz_zip_archive_file_stat st;
        if (!stat(index, st) || st.m_is_directory)
            return std::nullopt;
        std::string data(static_cast<size_t>(st.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&fZip, index, data.data(), data.size(), 0))
            return std::nullopt;
        return data;
    }

private:
    FILE* fFile = nullptr;
    mz_zip_archive fZip {};
    bool fOpen = false;
};

// A short description from a .nam file's metadata: "make model · type"
std::string namInfo(const std::string& bytes)
{
    try
    {
        const json root = json::parse(bytes);
        const auto meta = root.find("metadata");
        if (meta == root.end() || !meta->is_object())
            return "";
        auto field = [&meta](const char* key) {
            const auto it = meta->find(key);
            return it != meta->end() && it->is_string() ? it->get<std::string>() : std::string();
        };
        std::string gear = field("gear_make");
        const std::string model = field("gear_model");
        if (!model.empty())
            gear += (gear.empty() ? "" : " ") + model;
        if (gear.empty())
            gear = field("name");
        const std::string type = field("gear_type");
        if (!type.empty())
            gear += (gear.empty() ? "" : " · ") + type;
        return gear;
    }
    catch (const json::exception&)
    {
        return "";
    }
}

std::string describe(LibraryKind kind, const std::string& bytes)
{
    return kind == LibraryKind::Nam ? namInfo(bytes) : std::string();
}

std::string relativeFolder(const fs::path& root, const fs::path& dir)
{
    std::error_code ec;
    const fs::path rel = fs::relative(dir, root, ec);
    if (ec || rel.empty() || rel == ".")
        return "";
    const std::u8string text = rel.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace

std::optional<ArchivePath> splitArchivePath(const std::string& path)
{
    const size_t at = path.find(kArchiveSeparator);
    if (at == std::string::npos)
        return std::nullopt;
    return ArchivePath { path.substr(0, at), path.substr(at + kArchiveSeparator.size()) };
}

std::string hashBytes(std::string_view bytes)
{
    const XXH128_hash_t hash = XXH3_128bits(bytes.data(), bytes.size());
    char text[33];
    std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(hash.high64),
                  static_cast<unsigned long long>(hash.low64));
    return text;
}

std::optional<std::string> readLibraryFile(const std::string& path)
{
    if (const std::optional<ArchivePath> archive = splitArchivePath(path))
    {
        Zip zip(pathFromUtf8(archive->archive));
        if (!zip.isOpen())
            return std::nullopt;
        const int index = zip.find(archive->entry);
        if (index < 0)
            return std::nullopt;
        return zip.read(static_cast<mz_uint>(index));
    }
    return readFile(pathFromUtf8(path));
}

std::optional<std::string> hashLibraryFile(const std::string& path)
{
    const std::optional<std::string> bytes = readLibraryFile(path);
    if (!bytes)
        return std::nullopt;
    return hashBytes(*bytes);
}

std::optional<std::string> materializeLibraryFile(const std::string& path)
{
    const std::optional<ArchivePath> archive = splitArchivePath(path);
    if (!archive)
        return path;

    Zip zip(pathFromUtf8(archive->archive));
    if (!zip.isOpen())
        return std::nullopt;
    const int index = zip.find(archive->entry);
    mz_zip_archive_file_stat st;
    if (index < 0 || !zip.stat(static_cast<mz_uint>(index), st))
        return std::nullopt;

    // One folder per archive entry (and archive version), keeping the original file name
    const std::string key = hashBytes(path + "|" + std::to_string(modificationTime(pathFromUtf8(archive->archive))));
    const fs::path target = cacheDir() / "extracted" / key.substr(0, 16) / pathFromUtf8(archive->entry).filename();
    std::error_code ec;
    if (fs::exists(target, ec) && fs::file_size(target, ec) == st.m_uncomp_size)
        return pathToUtf8(target);

    const std::optional<std::string> bytes = zip.read(static_cast<mz_uint>(index));
    if (!bytes || !writeFileAtomic(target, *bytes))
        return std::nullopt;
    return pathToUtf8(target);
}

fs::path LibraryIndex::defaultPath(LibraryKind kind)
{
    return cacheDir() / (kind == LibraryKind::Nam ? "library-nam.json" : "library-ir.json");
}

void LibraryIndex::load(const fs::path& path)
{
    fEntries.clear();
    const std::optional<std::string> text = readFile(path);
    if (!text)
        return;
    try
    {
        const json root = json::parse(*text);
        for (const json& item : root.at("entries"))
        {
            LibraryEntry entry;
            entry.path = item.at("path").get<std::string>();
            entry.hash = item.at("hash").get<std::string>();
            entry.info = item.value("info", std::string());
            entry.size = item.at("size").get<uint64_t>();
            entry.mtime = item.at("mtime").get<int64_t>();
            fEntries[entry.path] = std::move(entry);
        }
    }
    catch (const json::exception&)
    {
        fEntries.clear(); // a broken cache only costs a full rescan
    }
}

bool LibraryIndex::save(const fs::path& path) const
{
    json entries = json::array();
    for (const auto& [key, entry] : fEntries)
    {
        entries.push_back({ { "path", entry.path },
                            { "hash", entry.hash },
                            { "info", entry.info },
                            { "size", entry.size },
                            { "mtime", entry.mtime } });
    }
    return writeFileAtomic(path, json { { "version", 1 }, { "entries", entries } }.dump());
}

const LibraryEntry* LibraryIndex::find(const std::string& path, uint64_t size, int64_t mtime) const
{
    const auto it = fEntries.find(path);
    if (it == fEntries.end() || it->second.size != size || it->second.mtime != mtime)
        return nullptr;
    return &it->second;
}

std::vector<LibraryEntry> scanLibrary(const fs::path& root, LibraryKind kind, LibraryIndex& index,
                                      const std::atomic<bool>* cancel)
{
    std::vector<LibraryEntry> found;
    const std::string_view ext = extensionFor(kind);
    auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };

    // Fills in hash and info, from the index when the file is unchanged
    auto complete = [&](LibraryEntry& entry, auto readBytes) {
        if (const LibraryEntry* cached = index.find(entry.path, entry.size, entry.mtime))
        {
            entry.hash = cached->hash;
            entry.info = cached->info;
            return true;
        }
        const std::optional<std::string> bytes = readBytes();
        if (!bytes)
            return false;
        entry.hash = hashBytes(*bytes);
        entry.info = describe(kind, *bytes);
        return true;
    };

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (cancelled())
            break;
        const fs::directory_entry& file = *it;
        std::error_code fileEc;
        if (!file.is_regular_file(fileEc))
            continue;
        const std::string name = pathToUtf8(file.path().filename());
        const std::string path = pathToUtf8(file.path());

        if (hasExtension(name, ext))
        {
            LibraryEntry entry;
            entry.path = path;
            entry.name = stem(name);
            entry.folder = relativeFolder(root, file.path().parent_path());
            entry.size = file.file_size(fileEc);
            entry.mtime = modificationTime(file.path());
            if (complete(entry, [&] { return readFile(file.path()); }))
                found.push_back(std::move(entry));
            continue;
        }

        if (!hasExtension(name, ".zip"))
            continue;
        Zip zip(file.path());
        if (!zip.isOpen())
            continue;
        const int64_t mtime = modificationTime(file.path());
        std::string zipFolder = relativeFolder(root, file.path().parent_path());
        zipFolder += (zipFolder.empty() ? "" : "/") + stem(name);
        for (mz_uint i = 0; i < zip.count() && !cancelled(); ++i)
        {
            mz_zip_archive_file_stat st;
            if (!zip.stat(i, st) || st.m_is_directory)
                continue;
            const std::string entryName = st.m_filename;
            if (!hasExtension(entryName, ext) || entryName.find("__MACOSX") != std::string::npos)
                continue;

            LibraryEntry entry;
            entry.path = path + std::string(kArchiveSeparator) + entryName;
            entry.name = stem(entryName);
            const size_t slash = entryName.find_last_of('/');
            entry.folder = zipFolder + (slash == std::string::npos ? "" : "/" + entryName.substr(0, slash));
            entry.size = st.m_uncomp_size;
            entry.mtime = mtime;
            if (!complete(entry, [&] { return zip.read(i); }))
                continue;

            found.push_back(std::move(entry));
        }
    }

    std::sort(found.begin(), found.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        const std::string fa = lower(a.folder), fb = lower(b.folder);
        if (fa != fb)
            return fa < fb;
        return lower(a.name) < lower(b.name);
    });

    if (!cancelled())
    {
        index.clear();
        for (const LibraryEntry& entry : found)
            index.put(entry);
    }
    return found;
}

} // namespace namsayin
