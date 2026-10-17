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
//   - the process and thread handles are closed after the resume; the job handle is owned by a static for the whole
//     session and never closed by us, so kill-on-close fires only when the game process ends
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

    /// "not started (why)", "running (pid N)", "ended" - for the log and the HUD
    [[nodiscard]] std::string Status();
}
