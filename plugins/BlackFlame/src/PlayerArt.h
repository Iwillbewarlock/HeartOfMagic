#pragma once

#include "Common.h"

// =============================================================================
// LAYER 4b - THE PLAYER'S BOUND FIRE WEAPONS AND FIRE LIGHT EFFECTS
// =============================================================================
// Generic rules, nothing per mod:
//   - A bound fire weapon: a weapon with the bound flag that an active Bound
//     Weapon-archetype effect the player cast on themselves names (EffectSetting
//     associatedForm), where that effect is a fire effect of a fire spell or the
//     weapon's enchantment carries a fire MGEF. The list is refreshed by the ticker
//     task; the projectile hook only compares FormIDs. Its arrows (ArrowProjectile,
//     weaponSource) count as the player's fire projectiles, and while stoked its 3D
//     in the player's bipeds (BIPOBJECT::partClone, 1st and 3rd person) is
//     recoloured (config boundWeapon).
//   - A Light-archetype fire effect the player cast on themselves: its light
//     (LightEffect::light, through the RTTI check) is tinted.
// Both are put back with the hand art when the buff ends (CasterArt.h).

namespace BlackFlame
{
    /// Any thread (the projectile hook): the arrow's weapon is on the cached bound fire weapon list
    [[nodiscard]] bool IsBoundFireArrow(RE::Projectile* a_projectile);

    /// Any thread: the weapon is on the cached bound fire weapon list (v2: the Launch fire check)
    [[nodiscard]] bool IsBoundFireWeapon(const RE::TESForm* a_weapon);

    /// Ticker task while stoked: bound fire weapons' 3D and fire light effects on the player
    void TickPlayerArt();

    /// The buff changed: on - refresh at once; off - forget the bound fire list
    void OnPlayerArtBuffChanged(bool a_on);

    /// At kPreLoadGame / kNewGame: forgets the bound fire list
    void ResetPlayerArt();
}
