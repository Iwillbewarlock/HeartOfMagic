#pragma once

// =============================================================================
// COPY MODEL PROBE VERIFY - verify before open: a candidate group's NIFs Demanded one by one (CopyGroups slices them)
// =============================================================================
// Single launch (design v3.2 §1): before a group of slots opens, every NIF it would show - each member's new model and
// muzzle, and an ADDN slot's new MPS - is asked of the model database the way LoadGraphics asks (BSModelDB::Demand, the
// record-form path, default arguments), on the main thread, after its file was registered (FileIndex). Only when every one
// parses does the group get its models, links and open gates. A NIF that fails marks its key failed (the ledger counts a
// session; two sessions make it terminal "parse") and the group is not opened this session. The slot's own model is never
// demanded while its gate is closed: a Demand of a path that is a gated slot's current model is counted (T0-W2: 0).

#include "Common.h"

#include <cstdint>
#include <string>

namespace BlackFlame::CopyModelProbe
{
    struct VerifyResult
    {
        bool ok = false;
        bool fault = false;   // the Demand faulted (SEH, logged)
        int code = 0;         // BSResource::ErrorCode
        bool node = false;    // a node came back
        long long ms = 0;
    };

    /// Main thread: BSModelDB::Demand(a_recordPath) now; the reference it hands back is given back at once
    [[nodiscard]] VerifyResult DemandOnce(const std::string& a_recordPath);

    /// "Demand(\"path\"): error code N, node yes|NULL, T ms"
    [[nodiscard]] std::string Describe(const std::string& a_recordPath, const VerifyResult& a_result);

    /// Demands of a path that was a gated slot's current model when asked (the invariant: 0)
    [[nodiscard]] std::uint32_t GatedSlotDemands() noexcept;

    /// Every Demand DemandOnce made this session
    [[nodiscard]] std::uint32_t VerifyDemands() noexcept;
}
