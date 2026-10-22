#pragma once

#include "Common.h"

// =============================================================================
// EQUIP ABILITY REFRESH - the player's fire equip abilities (EquipAbilityRules.h) follow the buff
// =============================================================================
// The engine asks GetHitEffectArt once when an effect starts, so an equip ability applied before the buff (Fire Storm's
// body art, applied when the spell is equipped) kept its original art for the whole buff (log 2026-10-23: "kept: not
// stoked" at equip, never asked again). On each buff edge the abilities the player has in the actor's spell list are
// removed and added again on the main thread, so the new effect asks again. An ability not in the spell list is left as
// is: the engine applied it some other way, and AddSpell would leave it on the actor for good. Only with copyHitArt or
// hitArt (otherwise the art slot keeps the engine's art either way).

namespace BlackFlame
{
    /// Post the refresh to the main-thread queue (any thread)
    void RequestEquipAbilityRefresh(bool a_stoked);
}
