#pragma once

#include "Common.h"

#include <cstdint>
#include <string>

// =============================================================================
// FILE PROBE GUARD CHECK - is this the engine's IndexGuard? (dev file visibility probe, FileProbeEngine.h)
// =============================================================================
// Structural checks, not byte patterns (no 1.6.1170 byte pattern is known here; its .text is encrypted on disk). They keep
// two arbitrary functions from being called as the guard; they cannot prove the functions right, and a wrong layout that
// passes them can still hang the game once the index lock is taken.

namespace BlackFlame::FileProbe::Engine::GuardCheck
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
