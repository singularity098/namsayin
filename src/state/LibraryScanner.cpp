// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "state/LibraryScanner.hpp"

#include "state/Paths.hpp"

namespace namsayin {

namespace {

std::vector<LibraryEntry> scanKind(const std::string& dir, LibraryKind kind, const std::atomic<bool>& cancel)
{
    if (dir.empty())
        return {};
    LibraryIndex index;
    index.load(LibraryIndex::defaultPath(kind));
    std::vector<LibraryEntry> entries = scanLibrary(pathFromUtf8(dir), kind, index, &cancel);
    if (!cancel)
        index.save(LibraryIndex::defaultPath(kind));
    return entries;
}

} // namespace

LibraryScanner::~LibraryScanner()
{
    stop();
}

void LibraryScanner::start(const std::string& namDir, const std::string& irDir)
{
    stop();
    fCancel = false;
    fBusy = true;
    fThread = std::thread([this, namDir, irDir] {
        Result result;
        result.nam = scanKind(namDir, LibraryKind::Nam, fCancel);
        result.ir = scanKind(irDir, LibraryKind::Ir, fCancel);
        if (!fCancel)
        {
            const std::lock_guard<std::mutex> lock(fMutex);
            fResult = std::move(result);
        }
        fBusy = false;
    });
}

std::optional<LibraryScanner::Result> LibraryScanner::poll()
{
    const std::lock_guard<std::mutex> lock(fMutex);
    std::optional<Result> result = std::move(fResult);
    fResult.reset();
    return result;
}

void LibraryScanner::wait()
{
    if (fThread.joinable())
        fThread.join();
}

void LibraryScanner::stop()
{
    fCancel = true;
    wait();
}

} // namespace namsayin
