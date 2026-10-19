#pragma once

// =============================================================================
// Toxicology - removing bottles of the copied poison
// =============================================================================
// Spec 5.6, 12: bottles go silently (RemoveItem, ITEM_REMOVE_REASON::kRemove),
// from a stack without the favorite hotkey (ExtraHotkey) first; the favorited
// stack shrinks only when nothing else is left (core NextDoseStack). When the
// copied poison runs out, the choice moves to the next poison.
//
// Threads: game main thread only (called from ToxMagicka's tasks).
// =============================================================================

#include <cstdint>

namespace ToxGame
{
    // Removes a_doses bottles of the copied poison (CurrentPoison). Each time the poison runs out,
    // OnSelectionMaybeChanged picks the next one and the rest come from it; doses left over when no
    // poison remains are dropped and logged.
    void UseDoses(std::int32_t a_doses);
}
