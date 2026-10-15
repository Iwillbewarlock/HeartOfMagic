#pragma once

#include "Common.h"

// =============================================================================
// LAYER 4b - THE PLAYER'S BOUND FIRE WEAPONS AND FIRE LIGHT EFFECTS
// =============================================================================
// Generic rules, nothing per mod:
//   - A bound fire weapon: a weapon with the bound flag that an active Bound
//     Weapon-archetype effect on the player names (EffectSetting associatedForm),
//     where that effect is a fire effect the player cast. Its arrows (ArrowProjectile,
//     weaponSource) count as the player's fire projectiles, and while stoked its 3D
//     in the player's bipeds (BIPOBJECT::partClone, 1st and 3rd person) is
//     recoloured (config boundWeapon).
//   - A Light-archetype fire effect the player cast on themselves: its light
//     (LightEffect::light, through the RTTI check) is tinted.
// Both are put back with the hand art when the buff ends (CasterArt.h).

namespace BlackFlame
{
    /// Any thread (the projectile hook): the arrow's weapon is a bound fire weapon of the player's
    [[nodiscard]] bool IsBoundFireArrow(RE::Projectile* a_projectile);

    /// Ticker task while stoked: bound fire weapons' 3D and fire light effects on the player
    void TickPlayerArt();
}
