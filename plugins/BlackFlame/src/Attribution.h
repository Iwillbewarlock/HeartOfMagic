#pragma once

#include "Common.h"

// =============================================================================
// ATTRIBUTION - is a projectile, explosion or hazard the player's
// =============================================================================
// One generic rule for every layer: the player's when its owner or shooter actor
// is the player, OR its ActorCause (TESObjectREFR::GetActorCause, vfunc 0x51 -
// Projectile, Explosion and Hazard keep one) names the player, OR the magic caster
// that made it is a NonActorMagicCaster whose blameActor is the player. The last
// two cover Spell.RemoteCast(source = an activator, blame = the player): the
// activator is the shooter/owner, the player only the cause or blame. Last, the
// reference or its owner is tagged by a script of the player's fire effect
// (ScriptTags.h). Which field matched is logged once per layer and field.

namespace BlackFlame
{
    enum class PlayerLink
    {
        kNone,
        kOwner,  // owner / shooter actor
        kCause,  // ActorCause::actor
        kBlame,  // NonActorMagicCaster::blameActor
        kTagged  // placed by, or the akSource of a cast from, a script of the player's fire effect
    };

    /// a_owner: the reference's owner or shooter (may be null or a non-actor); a_caster: the magic
    /// caster that made it, when known (an explosion's own NonActorMagicCaster)
    [[nodiscard]] PlayerLink LinkToPlayer(RE::TESObjectREFR* a_ref, const RE::TESObjectREFR* a_owner,
        RE::NonActorMagicCaster* a_caster = nullptr);

    [[nodiscard]] const char* PlayerLinkName(PlayerLink a_link) noexcept;

    /// The first time a_layer (a string literal) matched by a_link: one info line
    void LogLinkOnce(const char* a_layer, PlayerLink a_link);
}
