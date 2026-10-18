#pragma once

// =============================================================================
// BuilderSignal - the throttle event between the DLL and the grey copy builder (header only, no game types, C++17)
// =============================================================================
// A named, manual-reset event Local\HoMBlackFlame_Throttle_<pid of the DLL's process>. The DLL creates it before it
// starts the builder and sets it when the publish window closes (what the builder finishes after that is used only at
// the next launch, while it competes with play). The builder opens it with SYNCHRONIZE only (absent: never throttled,
// e.g. a run by hand) and checks it between objects; once set it releases its GPU device for the rest of the run and
// runs OpenMP on one thread (tools/blackflame-greybuild GreyBudget.h ThrottleSignal). bf-no-wait-proposal.md section 5.

#include <cstdint>
#include <string>

namespace BlackFlame::BuilderSignal
{
    inline constexpr const wchar_t* kThrottleEventPrefix = L"Local\\HoMBlackFlame_Throttle_";

    /// The throttle event's name for the DLL's process a_parentPid (the builder's --parent-pid)
    [[nodiscard]] inline std::wstring ThrottleEventName(std::uint32_t a_parentPid)
    {
        return std::wstring(kThrottleEventPrefix) + std::to_wstring(a_parentPid);
    }
}
