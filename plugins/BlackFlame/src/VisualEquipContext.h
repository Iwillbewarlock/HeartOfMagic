#pragma once

#include "Common.h"

namespace BlackFlame
{
    // Any hook thread. Checks the live selected spell identities, never a stale eligibility cache.
    // The caller separately gates buff state. Ambiguous shared parents keep the original.
    [[nodiscard]] bool IsPlayerVisualEquipEffect(RE::ActiveEffect* a_effect);

    // Main thread: a shared ability may survive a change of selected parent. Ask for its visuals
    // again on selection/world changes, including transitions back to a nonvisual parent.
    void TickVisualEquipContext();
    void ResetVisualEquipContext();
}
