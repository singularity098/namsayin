// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "state/UpdateCheck.hpp"

#include <mutex>

namespace namsayin {

// HTTPS GET for the update check. Windows: WinHTTP (with the system's proxy settings). macOS and Linux: the curl
// command (every Mac has it, and nearly every Linux desktop), so the plugin links no TLS library; without curl the
// check just finds nothing. Kept out of NAMsayinUI.cpp so <windows.h> stays out of the DPF code.
class HttpGet final : public HttpFetcher {
public:
    HttpGet() = default;
    std::optional<std::string> get(const char* url, size_t maxBytes) override;
    void cancel() override;

private:
    std::mutex fMutex;
    bool fCancelled = false;
#ifdef _WIN32
    void* fRequest = nullptr; // HINTERNET; closing it from cancel() aborts the request
#else
    int fPid = 0;             // the running curl, killed by cancel()
#endif
};

} // namespace namsayin
