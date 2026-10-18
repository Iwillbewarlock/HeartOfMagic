#pragma once

// =============================================================================
// BUILDER LAUNCHER - the builder child: CreateProcessW suspended, a job with kill-on-close and the memory cap, UniqueHandle
// =============================================================================
// Single launch (design v2 §2, v3 #8, v3.2 §8): at kDataLoaded, after request.json v5 and the dump are written and
// BuildProgress read progress.jsonl to its end, the builder (<DLL folder>\HeartOfMagic_BlackFlame\blackflame-greybuild.exe)
// starts as a child of the game:
//   - CreateProcessW suspended, BELOW_NORMAL_PRIORITY_CLASS, no window; only its log file handle is inherited
//     (PROC_THREAD_ATTRIBUTE_HANDLE_LIST): stdout and stderr go to <request folder>\builder.log
//   - a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE and JOB_OBJECT_LIMIT_JOB_MEMORY (config builderJobMemoryMiB, the
//     same number passed as --job-memory), the process assigned to it, then resumed; --parent-pid as a second guard
//   - before it: builder.log of the last launch renamed to builder.prev.log (an older prev replaced), and the throttle
//     event (BuilderSignal.h, manual reset, unset) created for the builder to open
//   - the thread handle is closed after the resume; the process handle is kept for the session (its exit code, logged
//     once when it ends: BuilderControl.h ExitCodeText); the job handle is owned by a static for the whole session and
//     never closed by us, so kill-on-close fires only when the game process ends
// When the publish window closes (MenuWatch::Close), ThrottleAfterWindow: the job gets JOB_OBJECT_LIMIT_PRIORITY_CLASS
// (idle) and a CPU hard cap of one logical CPU, and the throttle event is set (the builder then drops its GPU device and
// runs OpenMP on one thread); what it finishes after that is used only at the next launch.
// Command line (an assumption to reconcile with P1's builder: the P0 contract names no CLI):
//   "<exe>" --from-game "<request folder>" --staging "<staging folder>" --job-memory <MiB> --parent-pid <game pid>

#include "Common.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace BlackFlame::BuilderLauncher
{
    /// A Windows handle closed by its destructor (process, thread, job, file); move only
    class UniqueHandle
    {
    public:
        UniqueHandle() noexcept = default;
        explicit UniqueHandle(HANDLE a_handle) noexcept : m_handle(a_handle) {}
        ~UniqueHandle() { Reset(); }
        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;
        UniqueHandle(UniqueHandle&& a_other) noexcept : m_handle(a_other.Release()) {}
        UniqueHandle& operator=(UniqueHandle&& a_other) noexcept
        {
            if (this != &a_other) {
                Reset(a_other.Release());
            }
            return *this;
        }

        [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_handle && m_handle != INVALID_HANDLE_VALUE; }
        [[nodiscard]] HANDLE Release() noexcept
        {
            const auto handle = m_handle;
            m_handle = nullptr;
            return handle;
        }
        void Reset(HANDLE a_handle = nullptr) noexcept
        {
            if (a_handle == m_handle) {
                return;  // the handle it holds: never closed and kept
            }
            if (*this) {
                ::CloseHandle(m_handle);
            }
            m_handle = a_handle;
        }

    private:
        HANDLE m_handle = nullptr;
    };

    struct Launch
    {
        bool started = false;
        std::uint32_t pid = 0;
        std::string why;  // why not started
    };

    /// kDataLoaded: starts the builder (once a session); a_exe from SlotKeys::BuilderExePath
    Launch Start(const std::filesystem::path& a_exe, const std::filesystem::path& a_requestFolder,
        const std::filesystem::path& a_staging, std::uint32_t a_jobMemoryMiB);

    /// The builder's process is still in its job (any thread)
    [[nodiscard]] bool Running();

    /// "not started (why)", "running (pid N)", "ended (exit <ExitCodeText>)" - for the log
    [[nodiscard]] std::string Status();

    /// The publish window closed (any thread; returns at once - a few job and event calls, no wait): idle priority and a
    /// CPU hard cap of one logical CPU on the job, the throttle event set; logged with what was read back. Once a session;
    /// no builder, or one that already ended: nothing (debug level)
    void ThrottleAfterWindow();
}
