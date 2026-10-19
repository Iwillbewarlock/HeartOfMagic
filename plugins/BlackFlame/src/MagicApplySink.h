#pragma once

#include "Common.h"

#include "HitPathAttribution.h"

#include <string>

// =============================================================================
// MAGIC APPLY SINK (2026-10-20, design v2 round 1) - the elemental hits, recorded before FEC's Play
// =============================================================================
// FEC starts its corpse shaders (EffectShader.Play, the corpse mark's OwnedController) from its Papyrus
// OnMagicEffectApply handler, which may run before the ActiveEffect's hit shader start reaches NoteFireEffect
// (HitEffectHook's 0x0D thunk): an NPC's fire on the player's stoked kill could then still read "player stoked" and
// be marked grey. TESMagicEffectApplyEvent is dispatched by the engine to every sink in turn; the Papyrus VM's own
// sink only queues the script event, and the handler runs on a later VM update, so this sink's record is there
// before the handler (ordering to be confirmed by the run 8 "magic effect apply" lines against the "corpse mark" /
// "corpse not marked" lines' game time and thread). For an Actor target and a fire, frost or shock damage MGEF it
// records the caster's source kind into the same per-target memory as NoteFireEffect (HitPathAttribution.h
// RecordElementalHit, under its lock: the sink runs on the dispatching thread). NoteFireEffect stays: it knows the
// spell and so the exact "player stoked" test; the event carries no spell, so here the player's fire counts as stoked
// when its MGEF is a fire effect of some fire spell (FireSet IsFireEffect) while stoked - a proxy (an enchantment
// that uses a fire spell's own MGEF would pass; vanilla enchantments have MGEFs of their own; that MGEF's effect start
// then re-judges the apply event's record with the spell, RecordElementalHit). A fault in the handler skips that event
// (GuardedKeep). Lines: 40 a session for the player's stoked fire on living targets, 24 for any hit on a down target.
// Element (ElementOf): the resist value (ResistFire / ResistFrost / ResistShock) or the vanilla MagicDamageFire /
// MagicDamageFrost / MagicDamageShock keyword (frost and shock found by editor id at kDataLoaded; no FormIDs).

namespace BlackFlame::HitPath
{
    /// kDataLoaded, after BuildFireSets and HitPath::Install: finds the frost and shock keywords (NoteFireEffect reads them
    /// too) and adds the sink only when the corpse mark is installed (HitPath::CorpseMarkInstalled; one line either way)
    void RegisterMagicApplySink();

    /// Any thread: the MGEF's element (fire: FireSet IsFireMgefAny; frost / shock: resist value or keyword), kNone
    /// otherwise or for null
    [[nodiscard]] Detail::Element ElementOf(const RE::EffectSetting* a_effect);

    /// "magic effect apply (session): N events, N elemental on actors: recorded N, kept stoked N, not recorded N" (stats)
    [[nodiscard]] std::string MagicApplyStats();
}
