#pragma once

// =============================================================================
// BUILDER LOG PROBE - is a builder alive on this request folder? (its builder.log held for write)
// =============================================================================
// A running builder writes its stdout and stderr to <request folder>\builder.log through the handle it inherited from the
// game that started it (GENERIC_WRITE, sharing read and delete; BuilderLauncher.cpp MakeLog). The game's own copy of that
// handle closes right after the launch, so the file is held for write exactly while that builder lives. A second game
// on the same SKSE folder (or a relaunch while the last session's builder still runs) must not rotate that log away and
// start a second builder on the same staging folder (review Codex 2 of defe5ede). Header only, Windows, C++17: the DLL
// (BuilderLauncher::Start) and tools/blackflame-cosave-check (BuilderControlTests.h: the decision and real files).

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>

namespace BlackFlame::BuilderLogProbe
{
    enum class State : std::uint8_t
    {
        kNoLog,    // no builder.log (a first launch, or the folder is new)
        kFree,     // it opened for write with no write sharing: no process writes it
        kHeld,     // a sharing (or lock) violation: another process holds it for write - a live builder
        kUnknown   // another error (access denied, ...): not a live builder as far as can be told
    };

    /// The state for the Windows error of CreateFileW(builder.log, GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING); 0 = opened
    [[nodiscard]] inline State FromError(DWORD a_error) noexcept
    {
        switch (a_error) {
        case ERROR_SUCCESS:
            return State::kFree;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return State::kNoLog;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return State::kHeld;
        default:
            return State::kUnknown;
        }
    }

    /// Only a held log keeps the builder from starting (an unknown state starts it, and creating the log reports its own
    /// failure as before)
    [[nodiscard]] inline bool MayStart(State a_state) noexcept
    {
        return a_state != State::kHeld;
    }

    /// Opens a_log for write with read sharing only and closes it at once; a_error gets the Windows error (0: opened).
    /// Known limit: between this probe and the launch another game could start its own builder (a window of a few
    /// milliseconds at kDataLoaded); a lock file held for the builder's life would close it, not worth it for two games
    /// started in the same instant
    [[nodiscard]] inline State Probe(const std::filesystem::path& a_log, DWORD& a_error) noexcept
    {
        const HANDLE handle = ::CreateFileW(a_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        a_error = handle == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
        return FromError(a_error);
    }
}
