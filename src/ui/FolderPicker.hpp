// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace xythos {

// The system's folder picker, which DPF's file browser doesn't offer (it only picks files). It runs on its own
// thread; the UI polls it from its idle callback. Linux: the desktop portal (org.freedesktop.portal.FileChooser),
// then zenity or kdialog. Windows: IFileOpenDialog with FOS_PICKFOLDERS.
class FolderPicker
{
public:
    enum class Status {
        Idle,
        Open,        // the dialog is showing
        Picked,      // path() is the folder
        Cancelled,
        Unavailable, // no folder picker on this system: fall back to picking a file
    };

    FolderPicker() = default;
    ~FolderPicker();
    FolderPicker(const FolderPicker&) = delete;
    FolderPicker& operator=(const FolderPicker&) = delete;

    // Shows the picker, closing one that's already open. `window` is the native parent window (X11 window, HWND).
    void open(const std::string& title, const std::string& startDir, uintptr_t window);

    // The result once the dialog has closed. Reading a result other than Open resets the picker to Idle.
    Status poll(std::string& path);

private:
    void close();
    void run(std::string title, std::string startDir, uintptr_t window);
    void finish(Status status, std::string path = {});

    std::thread fThread;
    std::atomic<bool> fCancel { false };
    std::mutex fMutex;
    Status fStatus = Status::Idle;
    std::string fPath;
#ifdef _WIN32
    std::atomic<unsigned long> fThreadId { 0 };
#endif
};

} // namespace xythos
