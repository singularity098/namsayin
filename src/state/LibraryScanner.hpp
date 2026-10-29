// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "state/Library.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace xythos {

// Scans the NAM and IR folders on a background thread, using and updating the index caches. The UI starts a
// scan and polls for the result.
class LibraryScanner {
public:
    struct Result {
        std::vector<LibraryEntry> nam;
        std::vector<LibraryEntry> ir;
    };

    LibraryScanner() = default;
    ~LibraryScanner();

    LibraryScanner(const LibraryScanner&) = delete;
    LibraryScanner& operator=(const LibraryScanner&) = delete;

    // Starts a scan of both folders (an empty folder gives an empty list), cancelling one that is running
    void start(const std::string& namDir, const std::string& irDir);

    // The result of the last finished scan, once
    std::optional<Result> poll();

    bool busy() const { return fBusy.load(); }

    // Blocks until the running scan (if any) is done
    void wait();

private:
    void stop();

    std::thread fThread;
    std::atomic<bool> fCancel { false };
    std::atomic<bool> fBusy { false };
    std::mutex fMutex;
    std::optional<Result> fResult;
};

} // namespace xythos
