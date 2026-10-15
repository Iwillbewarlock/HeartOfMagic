#pragma once

#include "Common.h"

#include <utility>

// =============================================================================
// SEH GUARD - names the step where a hardware exception (an access violation)
// happened, with its code and faulting address as module + offset
// =============================================================================
// The build uses /EHa, so catch (...) also catches access violations - but then
// nothing says where. Guarded(step, f) runs f under __try/__except. Only faults
// (access violation, in-page error, illegal instruction, integer divide by zero,
// misalignment) are caught: the first one logs "<step>: hardware exception 0x...
// at SkyrimSE.exe+0x... (thread ...)", Guarded returns false so the caller skips
// the rest of its work, and the step is OFF for the rest of the session (its work
// may have stopped half done); later calls return false at once and are counted
// (logged at 1, 10, 100, ...). Anything else - C++ exceptions included - passes
// through to the caller's handlers. In this diagnostic build a fault inside engine
// code that holds an engine lock may hang the game rather than crash it.

namespace BlackFlame
{
    using SehBody = void (*)(void*);

    /// Runs a_body(a_context) under SEH; false on a fault (logged) and on every call after it.
    /// Not noexcept: a C++ exception from the body must reach the caller's catch.
    [[nodiscard]] bool RunSehGuarded(const char* a_step, SehBody a_body, void* a_context);

    template <class F>
    [[nodiscard]] bool Guarded(const char* a_step, F&& a_work)
    {
        auto body = [](void* a_context) { (*static_cast<std::remove_reference_t<F>*>(a_context))(); };
        return RunSehGuarded(a_step, body, &a_work);
    }
}
