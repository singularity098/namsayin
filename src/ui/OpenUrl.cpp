// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "OpenUrl.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <string>
#include <sys/wait.h>
extern char** environ;
#endif

namespace namsayin {

void openUrl(const char* url)
{
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    // Through a shell that backgrounds xdg-open (macOS: open), so there's no child process left to wait for. Only
    // called with URLs from this code, so quoting is enough.
#ifdef __APPLE__
    const std::string command = std::string("open '") + url + "' >/dev/null 2>&1 &";
#else
    const std::string command = std::string("xdg-open '") + url + "' >/dev/null 2>&1 &";
#endif
    const char* argv[] = { "sh", "-c", command.c_str(), nullptr };
    pid_t pid = 0;
    if (posix_spawnp(&pid, "sh", nullptr, nullptr, const_cast<char* const*>(argv), environ) == 0)
        waitpid(pid, nullptr, 0);
#endif
}

} // namespace namsayin
