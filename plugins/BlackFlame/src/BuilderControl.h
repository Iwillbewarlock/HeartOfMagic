#pragma once

// =============================================================================
// BuilderControl - the pure parts of BuilderLauncher: the throttle's CPU rate and the builder's exit code in words
// =============================================================================
// Header only, no game types, C++17 (tools/blackflame-cosave-check BuilderControlTests.h checks it, and pins the exit codes
// to the builder's own: GreyBuild.h kExitOk/kExitFail/kExitUsage, GreyBudget.h kExitParentGone). bf-no-wait-proposal.md
// sections 4 (F4 fix) and 5.

#include <cstdint>
#include <cstdio>
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
}
