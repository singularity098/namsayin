// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "HttpGet.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <vector>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace xythos {

#ifdef _WIN32

namespace {

std::wstring widen(const char* text)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring wide(size > 0 ? size - 1 : 0, L'\0');
    if (size > 1)
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), size);
    return wide;
}

} // namespace

std::optional<std::string> HttpGet::get(const char* url, size_t maxBytes)
{
    const std::wstring wideUrl = widen(url);
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        return std::nullopt;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo != nullptr)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    const std::wstring agent = widen((std::string("Xythos/") + kAppVersion).c_str());
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) // before Windows 8.1 there's no automatic proxy
        session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return std::nullopt;
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection == nullptr
        ? nullptr
        : WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    {
        std::lock_guard lock(fMutex);
        if (fCancelled && request != nullptr)
        {
            WinHttpCloseHandle(request);
            request = nullptr;
        }
        fRequest = request;
    }

    std::optional<std::string> body;
    if (request != nullptr
        && WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(request, nullptr))
    {
        DWORD status = 0, size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)
            && status == 200)
        {
            std::string data;
            std::vector<char> buffer(16384);
            bool ok = true;
            while (true)
            {
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
                {
                    ok = false;
                    break;
                }
                if (read == 0)
                    break;
                data.append(buffer.data(), read);
                if (data.size() > maxBytes)
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                body = std::move(data);
        }
    }

    {
        // cancel() may have closed the request already (that's how it aborts it)
        std::lock_guard lock(fMutex);
        if (fRequest != nullptr)
            WinHttpCloseHandle(fRequest);
        fRequest = nullptr;
        if (fCancelled)
            body.reset();
    }
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return body;
}

void HttpGet::cancel()
{
    std::lock_guard lock(fMutex);
    fCancelled = true;
    if (fRequest != nullptr)
        WinHttpCloseHandle(fRequest);
    fRequest = nullptr;
}

#else

std::optional<std::string> HttpGet::get(const char* url, size_t maxBytes)
{
    int fds[2];
    if (pipe(fds) != 0)
        return std::nullopt;
    // not inherited by anything else the host starts meanwhile; dup2 gives curl its own copy as stdout
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    const std::string agent = std::string("Xythos/") + kAppVersion;
#ifdef __APPLE__
    const char* curl = "/usr/bin/curl";
#else
    const char* curl = "curl";
#endif
    // -f: HTTP errors give no output; HTTPS only, redirects included
    const char* argv[] = { curl, "-sfL", "--max-time", "15", "--proto", "=https", "--proto-redir", "=https",
                           "-H", "Accept: application/vnd.github+json", "-A", agent.c_str(), url, nullptr };

    pid_t pid = 0;
    int spawned = -1;
    {
        std::lock_guard lock(fMutex);
        if (!fCancelled)
            spawned = posix_spawnp(&pid, curl, &actions, nullptr, const_cast<char* const*>(argv), environ);
        if (spawned == 0)
            fPid = pid;
    }
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (spawned != 0)
    {
        close(fds[0]);
        return std::nullopt;
    }

    std::string data;
    char buffer[16384];
    bool tooLong = false;
    while (true)
    {
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        data.append(buffer, static_cast<size_t>(n));
        if (data.size() > maxBytes)
        {
            tooLong = true;
            break;
        }
    }
    close(fds[0]);

    bool cancelled = false;
    {
        // once it's reaped its pid may be reused, so cancel() must not kill it after this
        std::lock_guard lock(fMutex);
        fPid = 0;
        cancelled = fCancelled;
        if (tooLong)
            kill(pid, SIGKILL);
    }
    int status = 0;
    pid_t waited;
    do
        waited = waitpid(pid, &status, 0);
    while (waited < 0 && errno == EINTR);
    if (cancelled || tooLong)
        return std::nullopt;
    // a host that ignores SIGCHLD has its children reaped for it (ECHILD): then the output has to speak for itself
    if (waited == pid && !(WIFEXITED(status) && WEXITSTATUS(status) == 0))
        return std::nullopt;
    return data;
}

void HttpGet::cancel()
{
    std::lock_guard lock(fMutex);
    fCancelled = true;
    if (fPid > 0)
        kill(fPid, SIGKILL);
}

#endif

} // namespace xythos
