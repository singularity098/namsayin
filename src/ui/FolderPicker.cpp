// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "FolderPicker.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <vector>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#ifdef HAVE_DBUS
#include <dbus/dbus.h>
#endif
extern char** environ;
#endif

namespace xythos {

FolderPicker::~FolderPicker()
{
    close();
}

void FolderPicker::open(const std::string& title, const std::string& startDir, uintptr_t window)
{
    close();
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fStatus = Status::Open;
        fPath.clear();
    }
    fCancel = false;
    fThread = std::thread(&FolderPicker::run, this, title, startDir, window);
}

FolderPicker::Status FolderPicker::poll(std::string& path)
{
    std::lock_guard<std::mutex> lock(fMutex);
    const Status status = fStatus;
    if (status == Status::Open || status == Status::Idle)
        return status;
    path = fPath;
    fStatus = Status::Idle;
    return status;
}

void FolderPicker::finish(Status status, std::string path)
{
    if (fCancel)
        return;
    std::lock_guard<std::mutex> lock(fMutex);
    fStatus = status;
    fPath = std::move(path);
}

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::string narrow(const wchar_t* text)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

BOOL CALLBACK closeWindow(HWND window, LPARAM)
{
    PostMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
    PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

} // namespace

void FolderPicker::close()
{
    if (!fThread.joinable())
        return;
    fCancel = true;
    // the dialog runs its own message loop on the picker thread: ask its windows to close
    if (const DWORD id = fThreadId.load())
        EnumThreadWindows(id, closeWindow, 0);
    fThread.join();
    fThreadId = 0;
    std::lock_guard<std::mutex> lock(fMutex);
    fStatus = Status::Idle;
}

void FolderPicker::run(std::string title, std::string startDir, uintptr_t window)
{
    fThreadId = GetCurrentThreadId();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
    {
        finish(Status::Unavailable);
        return;
    }
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        CoUninitialize();
        finish(Status::Unavailable);
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    const std::wstring titleW = widen(title);
    dialog->SetTitle(titleW.c_str());
    if (!startDir.empty())
    {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(widen(startDir).c_str(), nullptr, IID_PPV_ARGS(&folder))))
        {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    Status status = Status::Cancelled;
    std::string path;
    if (SUCCEEDED(dialog->Show(reinterpret_cast<HWND>(window))))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
            {
                path = narrow(name);
                status = Status::Picked;
                CoTaskMemFree(name);
            }
            item->Release();
        }
    }
    dialog->Release();
    CoUninitialize();
    finish(status, std::move(path));
}

#else

namespace {

// Runs a picker program and returns its first output line. `exitCode` is -1 if it couldn't run.
std::string runPicker(const std::vector<std::string>& args, const std::atomic<bool>& cancel, int& exitCode)
{
    exitCode = -1;
    int fds[2];
    if (pipe(fds) != 0)
        return {};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    std::vector<char*> argv;
    for (const std::string& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (error != 0)
    {
        ::close(fds[0]);
        return {};
    }

    std::string output;
    char buffer[1024];
    for (;;)
    {
        if (cancel)
        {
            kill(pid, SIGTERM);
            break;
        }
        pollfd pfd { fds[0], POLLIN, 0 };
        const int ready = ::poll(&pfd, 1, 100);
        if (ready < 0 && errno != EINTR)
            break;
        if (ready <= 0)
            continue;
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n <= 0)
            break;
        output.append(buffer, static_cast<size_t>(n));
    }
    ::close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const size_t end = output.find('\n');
    if (end != std::string::npos)
        output.resize(end);
    return output;
}

#ifdef HAVE_DBUS
int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// file:///a%20b -> /a b; empty if it isn't a local file URI
std::string pathFromUri(const char* uri)
{
    constexpr const char* prefix = "file://";
    if (std::strncmp(uri, prefix, std::strlen(prefix)) != 0)
        return {};
    std::string path;
    for (const char* s = uri + std::strlen(prefix); *s != '\0'; ++s)
    {
        if (*s == '%' && hexDigit(s[1]) >= 0 && hexDigit(s[2]) >= 0)
        {
            path += static_cast<char>(hexDigit(s[1]) * 16 + hexDigit(s[2]));
            s += 2;
        }
        else
        {
            path += *s;
        }
    }
    return path;
}

void appendDictEntry(DBusMessageIter& dict, const char* key, int type, const char* signature, const void* value,
                     int arrayLength = -1)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
    if (arrayLength >= 0)
    {
        DBusMessageIter array;
        dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, signature + 1, &array);
        dbus_message_iter_append_fixed_array(&array, type, value, arrayLength);
        dbus_message_iter_close_container(&variant, &array);
    }
    else
    {
        dbus_message_iter_append_basic(&variant, type, value);
    }
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);
}

// The first of the "uris" in a portal Response's results
std::string uriFromResponse(DBusMessage* message, uint32_t& response)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32)
        return {};
    dbus_message_iter_get_basic(&iter, &response);
    if (!dbus_message_iter_next(&iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return {};
    DBusMessageIter dict;
    dbus_message_iter_recurse(&iter, &dict);
    for (; dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&dict))
    {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);
        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        if (key == nullptr || std::strcmp(key, "uris") != 0 || !dbus_message_iter_next(&entry))
            continue;
        DBusMessageIter variant, uris;
        dbus_message_iter_recurse(&entry, &variant);
        if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY)
            return {};
        dbus_message_iter_recurse(&variant, &uris);
        if (dbus_message_iter_get_arg_type(&uris) != DBUS_TYPE_STRING)
            return {};
        const char* uri = nullptr;
        dbus_message_iter_get_basic(&uris, &uri);
        return uri ? uri : "";
    }
    return {};
}

// Asks the desktop portal for a folder. False if there's no portal (or it doesn't pick folders).
bool portalPick(const std::string& title, const std::string& startDir, uintptr_t window,
                const std::atomic<bool>& cancel, bool& picked, std::string& path)
{
    DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, nullptr);
    if (connection == nullptr)
        return false;
    dbus_connection_set_exit_on_disconnect(connection, false);
    bool handled = false;

    // folders need version 3 of the FileChooser interface
    uint32_t version = 0;
    if (DBusMessage* get = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                                        "/org/freedesktop/portal/desktop",
                                                        "org.freedesktop.DBus.Properties", "Get"))
    {
        const char* interface = "org.freedesktop.portal.FileChooser";
        const char* property = "version";
        dbus_message_append_args(get, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);
        if (DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, get, 2000, nullptr))
        {
            DBusMessageIter iter, variant;
            if (dbus_message_iter_init(reply, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
            {
                dbus_message_iter_recurse(&iter, &variant);
                if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_UINT32)
                    dbus_message_iter_get_basic(&variant, &version);
            }
            dbus_message_unref(reply);
        }
        dbus_message_unref(get);
    }

    if (version >= 3)
    {
        // our connection only gets the Response signals of its own requests
        dbus_bus_add_match(connection,
                           "type='signal',interface='org.freedesktop.portal.Request',member='Response'", nullptr);
        DBusMessage* call = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                                         "/org/freedesktop/portal/desktop",
                                                         "org.freedesktop.portal.FileChooser", "OpenFile");
        char parent[32];
        std::snprintf(parent, sizeof(parent), "x11:%llx", static_cast<unsigned long long>(window));
        const char* parentPtr = parent;
        const char* titlePtr = title.c_str();
        dbus_message_append_args(call, DBUS_TYPE_STRING, &parentPtr, DBUS_TYPE_STRING, &titlePtr, DBUS_TYPE_INVALID);
        DBusMessageIter iter, dict;
        dbus_message_iter_init_append(call, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
        const dbus_bool_t yes = true;
        appendDictEntry(dict, "directory", DBUS_TYPE_BOOLEAN, "b", &yes);
        appendDictEntry(dict, "modal", DBUS_TYPE_BOOLEAN, "b", &yes);
        if (!startDir.empty())
        {
            const char* bytes = startDir.c_str(); // a null-terminated byte array
            appendDictEntry(dict, "current_folder", DBUS_TYPE_BYTE, "ay", &bytes,
                            static_cast<int>(startDir.size() + 1));
        }
        dbus_message_iter_close_container(&iter, &dict);

        std::string handle;
        if (DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, call, 5000, nullptr))
        {
            const char* object = nullptr;
            if (dbus_message_get_args(reply, nullptr, DBUS_TYPE_OBJECT_PATH, &object, DBUS_TYPE_INVALID) && object)
                handle = object;
            dbus_message_unref(reply);
        }
        dbus_message_unref(call);

        if (!handle.empty())
        {
            handled = true;
            while (!cancel && dbus_connection_read_write(connection, 100))
            {
                DBusMessage* message = dbus_connection_pop_message(connection);
                if (message == nullptr)
                    continue;
                const bool response = dbus_message_is_signal(message, "org.freedesktop.portal.Request", "Response");
                if (response)
                {
                    uint32_t code = 1;
                    path = pathFromUri(uriFromResponse(message, code).c_str());
                    picked = code == 0 && !path.empty();
                }
                dbus_message_unref(message);
                if (response)
                    break;
            }
            if (cancel)
            {
                if (DBusMessage* closeCall = dbus_message_new_method_call(
                        "org.freedesktop.portal.Desktop", handle.c_str(), "org.freedesktop.portal.Request", "Close"))
                {
                    dbus_connection_send(connection, closeCall, nullptr);
                    dbus_connection_flush(connection);
                    dbus_message_unref(closeCall);
                }
            }
        }
    }
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
    return handled;
}
#endif

} // namespace

void FolderPicker::close()
{
    if (!fThread.joinable())
        return;
    fCancel = true;
    fThread.join();
    std::lock_guard<std::mutex> lock(fMutex);
    fStatus = Status::Idle;
}

void FolderPicker::run(std::string title, std::string startDir, uintptr_t window)
{
    std::string path;
#ifdef HAVE_DBUS
    bool picked = false;
    if (portalPick(title, startDir, window, fCancel, picked, path))
    {
        finish(picked ? Status::Picked : Status::Cancelled, std::move(path));
        return;
    }
#endif

    std::string start = startDir;
    if (!start.empty() && start.back() != '/')
        start += '/';
#ifdef __APPLE__
    // the system's folder dialog through AppleScript: it runs in its own process, so it needs nothing from the
    // host's main thread (exit code 1 when cancelled)
    (void)window;
    const auto quoted = [](const std::string& text) {
        std::string out = "\"";
        for (const char c : text)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        return out + "\"";
    };
    std::string script = "POSIX path of (choose folder with prompt " + quoted(title);
    if (!start.empty())
        script += " default location (POSIX file " + quoted(start) + ")";
    script += ")";
    const std::vector<std::vector<std::string>> pickers = { { "osascript", "-e", script } };
#else
    char parent[32];
    std::snprintf(parent, sizeof(parent), "%llu", static_cast<unsigned long long>(window));
    const std::vector<std::vector<std::string>> pickers = {
        { "zenity", "--file-selection", "--directory", "--title=" + title, "--filename=" + start },
        { "kdialog", "--attach", parent, "--title", title, "--getexistingdirectory", start.empty() ? "." : start },
    };
#endif
    for (const std::vector<std::string>& args : pickers)
    {
        int exitCode = -1;
        path = runPicker(args, fCancel, exitCode);
        if (fCancel)
            return;
        if (exitCode == 0 && !path.empty())
        {
            if (path.size() > 1 && path.back() == '/')
                path.pop_back(); // (AppleScript's folders end in one)
            finish(Status::Picked, std::move(path));
            return;
        }
        if (exitCode == 1)
        {
            finish(Status::Cancelled);
            return;
        }
        // 127 or no exit code: the program isn't there (or failed); try the next one
    }
    finish(Status::Unavailable);
}

#endif

} // namespace xythos
