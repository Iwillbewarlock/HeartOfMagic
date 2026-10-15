#pragma once

#include "Common.h"

#include <utility>

// =============================================================================
// SEH GUARD - names the step where a hardware exception (an access violation)
// happened, with its code and faulting address as module + offset
// =============================================================================
// The build uses /EHa, so catch (...) also catches access violations - but then
// nothing says where. Guarded(step, f) runs f under __try/__except: on a hardware
// exception it logs, once per step name, "<step>: exception 0x... at
// SkyrimSE.exe+0x... (thread ...)" and returns false so the caller can skip the
// rest of its work. C++ exceptions pass through untouched.

namespace BlackFlame
{
    using SehBody = void (*)(void*);

    /// Runs a_body(a_context) under SEH; false (logged once per a_step) on a hardware exception
    [[nodiscard]] bool RunSehGuarded(const char* a_step, SehBody a_body, void* a_context) noexcept;

    template <class F>
    [[nodiscard]] bool Guarded(const char* a_step, F&& a_work)
    {
        auto body = [](void* a_context) { (*static_cast<std::remove_reference_t<F>*>(a_context))(); };
        return RunSehGuarded(a_step, body, &a_work);
    }
}
