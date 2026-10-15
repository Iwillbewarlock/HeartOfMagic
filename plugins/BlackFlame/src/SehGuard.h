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

    enum class SehPolicy
    {
        kDisableStep,  // the default: a step that faulted is off for the session
        kKeepStep      // only this call is skipped (per-item restores); faults counted and logged
    };

    /// Runs a_body(a_context) under SEH; false on a fault (logged). kDisableStep: false on every call after
    /// it too; kKeepStep: only this call is skipped, faults counted and logged at 1, 10, 100...
    /// Not noexcept: a C++ exception from the body must reach the caller's catch.
    [[nodiscard]] bool RunSehGuarded(const char* a_step, SehBody a_body, void* a_context,
        SehPolicy a_policy = SehPolicy::kDisableStep);

    template <class F>
    [[nodiscard]] bool Guarded(const char* a_step, F&& a_work)
    {
        auto body = [](void* a_context) { (*static_cast<std::remove_reference_t<F>*>(a_context))(); };
        return RunSehGuarded(a_step, body, &a_work);
    }

    /// Guarded, but a fault skips only this call: the step stays on (restores, item by item)
    template <class F>
    [[nodiscard]] bool GuardedKeep(const char* a_step, F&& a_work)
    {
        auto body = [](void* a_context) { (*static_cast<std::remove_reference_t<F>*>(a_context))(); };
        return RunSehGuarded(a_step, body, &a_work, SehPolicy::kKeepStep);
    }
}
