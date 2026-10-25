#pragma once

// =============================================================================
// FILE INDEX GUARD CHECK - the structural checks that gate RegisterLoose (from FileProbeGuardCheck)
// =============================================================================
// Is this the engine's IndexGuard? Structural checks, not byte patterns, so that they hold on builds whose code was not
// read (1.7.104's code passes them on reading, 2026-10-25). They keep two arbitrary functions from being called as the
// guard; they cannot prove the functions right, and a wrong layout that passes them can still hang the game once the
// index lock is taken. Lifted from the spike's FileProbeGuardCheck (branch feature/black-flame-spike-s1, run 3 passed
// them on 1.6.1170).

#include "Common.h"

#include <cstdint>
#include <string>

namespace BlackFlame::FileIndexGuardCheck
{
    /// a_function's first bytes hold a REX.W mov (8B / 89) that is rip-relative to a_manager or uses the guard's +0xB8
    [[nodiscard]] bool TouchesManager(std::uintptr_t a_function, std::uintptr_t a_manager);

    /// a_function reads a field of the guard pointer it was given (rcx, or a register rcx was copied to) inside the guard
    [[nodiscard]] bool ReadsOwnGuard(std::uintptr_t a_function);

    /// The body behind one leading E9 rel32 / FF 25 [rip+disp32] jump, or a_function itself
    [[nodiscard]] std::uintptr_t FollowJump(std::uintptr_t a_function);

    /// "SkyrimSE+0x... [bytes]" and, when a jump was followed, "-> jump to SkyrimSE+0x... [bytes]"
    [[nodiscard]] std::string BodyText(std::uintptr_t a_function, std::uintptr_t a_body);
}
