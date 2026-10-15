#pragma once

#include "Common.h"

// =============================================================================
// ATTRIBUTION - is a projectile, explosion or hazard the player's
// =============================================================================
// One generic rule for every layer. The player's when:
//   - its owner or shooter is the player, OR
//   - the magic caster that made it is a NonActorMagicCaster whose blameActor is
//     the player (Spell.RemoteCast from an activator with the player as blame: an
//     explosion's own caster, or the ExtraMagicCaster on a non-actor shooter), OR
//   - the reference or its owner is tagged by a script of the player's fire effect
//     (ScriptTags.h).
// The ActorCause (TESObjectREFR::GetActorCause, vfunc 0x51; Projectile and
// Explosion override it, Hazard only SetActorCause) is NOT a rule (round 9): a
// fire trap the player set off may carry the player as its cause, and vanilla
// fire traps are Spell-type spells with fire effects, so they would turn grey.
// A cause naming the player with no rule matched is logged once per layer.
// Which rule matched is logged once per layer and rule.

namespace BlackFlame
{
    enum class PlayerLink
    {
        kNone,
        kOwner,  // owner / shooter actor
        kBlame,  // NonActorMagicCaster::blameActor
        kTagged  // placed by, or the akSource of a cast from, a script of the player's fire effect
    };

    /// a_layer: a string literal for the logs; a_owner: the reference's owner or shooter (may be null
    /// or a non-actor); a_caster: the magic caster that made it, when known (an explosion's own)
    [[nodiscard]] PlayerLink LinkToPlayer(const char* a_layer, RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_owner,
        RE::NonActorMagicCaster* a_caster = nullptr);

    /// The NonActorMagicCaster on a non-actor reference (its ExtraMagicCaster), or null
    [[nodiscard]] RE::NonActorMagicCaster* NonActorCasterOf(RE::TESObjectREFR* a_ref);

    [[nodiscard]] const char* PlayerLinkName(PlayerLink a_link) noexcept;

    /// The first time a_layer (a string literal) matched by a_link: one info line
    void LogLinkOnce(const char* a_layer, PlayerLink a_link);
}
