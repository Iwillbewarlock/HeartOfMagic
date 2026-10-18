#pragma once

// =============================================================================
// BuilderControl - the pure parts of BuilderLauncher: the throttle's CPU rate and the builder's exit code in words
// =============================================================================
// Header only, no game types, C++17 (tools/blackflame-cosave-check BuilderControlTests.h and IdentityTests.h check it, and
// pin the exit codes to the builder's own: GreyBuild.h kExitOk/kExitFail/kExitUsage, GreyBudget.h kExitParentGone).
// bf-no-wait-proposal.md sections 4 (F4 fix) and 5. Also: the builder identity's kind and the player's line for it (SlotKeys,
// SingleLaunch), and the throttle's hand-off when the launcher's lock is busy (BuilderLauncher::ThrottleAfterWindow).

#include "CopyContractKeys.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>

namespace BlackFlame::BuilderControl
{
    // The builder's exit codes (tools/blackflame-greybuild; the cosave-check static_asserts they still match)
    inline constexpr std::uint32_t kExitOk = 0;
    inline constexpr std::uint32_t kExitFail = 1;
    inline constexpr std::uint32_t kExitUsage = 2;
    inline constexpr std::uint32_t kExitParentGone = 3;
    inline constexpr std::uint32_t kStillActive = 259;  // STILL_ACTIVE: GetExitCodeProcess on a process not yet ended

    // JOBOBJECT_CPU_RATE_CONTROL_INFORMATION::CpuRate counts cycles per 10,000 of the whole machine (Win32 docs: "a
    // percentage times 100"; the SDK header has no unit comment); 0 is refused by SetInformationJobObject
    inline constexpr std::uint32_t kCpuRateWholeMachine = 10000;

    /// The hard cap's CpuRate for one logical CPU of a_logicalCpus (0 read as 1); never 0
    [[nodiscard]] inline std::uint32_t CpuRateForOneCpu(std::uint32_t a_logicalCpus) noexcept
    {
        const std::uint32_t cpus = a_logicalCpus == 0 ? 1 : a_logicalCpus;
        const std::uint32_t rate = kCpuRateWholeMachine / cpus;
        return rate == 0 ? 1 : rate;
    }

    /// "0 (ok: every object has a line)", ..., "0xC0000005 (terminated or crashed)"
    [[nodiscard]] inline std::string ExitCodeText(std::uint32_t a_code)
    {
        switch (a_code) {
        case kExitOk:
            return "0 (ok: every object has a line)";
        case kExitFail:
            return "1 (failed: the run could not go on; builder.log says why)";
        case kExitUsage:
            return "2 (refused: bad arguments or a request that is not version 5)";
        case kExitParentGone:
            return "3 (stopped: the parent process is gone)";
        default:
            break;
        }
        char hex[16]{};
        std::snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned>(a_code));
        return std::string(hex) + " (terminated or crashed)";
    }

    // What the DLL made of the builder exe at kDataLoaded (SlotKeys::Read)
    enum class BuilderIdentityKind : std::uint8_t
    {
        kMissing,       // the exe does not open (or the DLL's own path did not read)
        kUnreadable,    // there, but it does not load as a data file, has no identity resource, or a malformed one
        kOtherVersion,  // a valid identity of another output version
        kDev,           // this version, a dev build ("<n>+dev<hex>"): taken, logged at warn level
        kRelease        // this version, a release build: taken
    };

    /// a_text: the identity resource's bytes (nullopt: not read). Taken (kDev, kRelease) exactly when
    /// CopyContractIdentity.h BuilderIdentityFault finds nothing wrong
    [[nodiscard]] inline BuilderIdentityKind ClassifyIdentity(bool a_exeFound, const std::optional<std::string>& a_text,
        std::uint32_t a_ownVersion = CopyContract::kBuilderOutputVersion)
    {
        if (!a_exeFound) {
            return BuilderIdentityKind::kMissing;
        }
        const auto version = a_text ? CopyContract::OutputIdentityVersion(*a_text) : std::nullopt;
        if (!version) {
            return BuilderIdentityKind::kUnreadable;
        }
        if (*version != a_ownVersion) {
            return BuilderIdentityKind::kOtherVersion;
        }
        return CopyContract::IsDevIdentity(*a_text) ? BuilderIdentityKind::kDev : BuilderIdentityKind::kRelease;
    }

    [[nodiscard]] inline bool IdentityTaken(BuilderIdentityKind a_kind) noexcept
    {
        return a_kind == BuilderIdentityKind::kDev || a_kind == BuilderIdentityKind::kRelease;
    }

    /// The player's line at the main menu when no key is made this session (empty: the builder was taken)
    [[nodiscard]] inline std::string BuilderNotice(BuilderIdentityKind a_kind)
    {
        switch (a_kind) {
        case BuilderIdentityKind::kMissing:
            return "Black Flame: the grey copy builder was not found; only copies built before are shown";
        case BuilderIdentityKind::kUnreadable:
            return "Black Flame: the grey copy builder could not be read; only copies built before are shown";
        case BuilderIdentityKind::kOtherVersion:
            return "Black Flame: the grey copy builder is from another version; only copies built before are shown";
        default:
            return {};
        }
    }

    /// Added to the "grey copies are off this session (...)" line (empty: the builder was taken)
    [[nodiscard]] inline std::string BuilderRouteOffClause(BuilderIdentityKind a_kind)
    {
        switch (a_kind) {
        case BuilderIdentityKind::kMissing:
            return "; the grey copy builder was not found either";
        case BuilderIdentityKind::kUnreadable:
            return "; the grey copy builder could not be read either";
        case BuilderIdentityKind::kOtherVersion:
            return "; the grey copy builder is from another version too";
        default:
            return {};
        }
    }

    enum class HandOff : std::uint8_t
    {
        kRanHere,    // the lock was free: a_locked ran on the caller's thread
        kHandedOff,  // the lock was busy: a_submit gave the job to another thread (which may wait for the lock)
        kPending     // busy and a_submit refused: the job waits for the next lock holder that honours the request
    };

    /// Never waits on a_mutex: a_locked(lock) when try_lock takes it at once (a_locked gets the std::unique_lock and
    /// unlocks it before its slow part: logging, kernel calls), else a_submit() (true: handed to another thread)
    template <class Mutex, class Locked, class Submit>
    [[nodiscard]] HandOff RunOrHandOff(Mutex& a_mutex, Locked&& a_locked, Submit&& a_submit)
    {
        std::unique_lock<Mutex> lock(a_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            a_locked(lock);
            return HandOff::kRanHere;
        }
        return a_submit() ? HandOff::kHandedOff : HandOff::kPending;
    }

    /// Under the lock: true once, for the first holder that sees a_requested set while a_done is false (the request is
    /// consumed either way, so later holders skip it at once)
    [[nodiscard]] inline bool TakeOnce(std::atomic<bool>& a_requested, bool& a_done) noexcept
    {
        if (!a_requested.exchange(false) || a_done) {
            return false;
        }
        a_done = true;
        return true;
    }
}
