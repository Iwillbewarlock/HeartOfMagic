#pragma once

#include "Common.h"

// =============================================================================
// LANTERN DAMAGE - the worn lantern's black flame on the player's fire hits (design v3 2026-10-21, section 5)
// =============================================================================
// HitEventSink (HitEvents.cpp, any thread) hands every TESHitEvent to OnHit: only FormID work happens there (the
// source classified once per id and cached, LanternDamageRules.h) and the per-target re-apply throttle under a mutex.
// An accepted hit queues a game-thread task: target an Actor, alive, not the player; the buff on and the player's active
// effect HoMBF_LanternEffect (0x810) the reason; then the hit line, the burn noted, and HoMBF_LanternBurnSpell (0x812,
// Ignore Resistance, a 2 s damage over time) cast on it with (max health x lanternBurnPct + lanternBurnFlat) / burn
// seconds as its per-second magnitude, the player blamed (kill credit); the burn line counts the 0x813 effects on the
// target after the cast. A re-cast only restarts the burn (the engine replaces the same spell from the same caster). A
// TESDeathEvent of a target that burned in the last burn seconds + 1 s logs the kill. Off for the session without the
// lantern records, with lanternDamage false, with a burn spell duration not above 0, or without the main-thread queue
// (DisableEngineWrites).

namespace BlackFlame::LanternDamage
{
    /// kDataLoaded, after LoadForms and BuildFireSets: the burn seconds, the fire hazards' spells, the death sink
    void Init();

    /// HitEventSink::ProcessEvent (any thread; nothing throws out of the caller's try)
    void OnHit(const RE::TESHitEvent& a_event);

    /// Message handler, world torn down: the throttle and the recent burn table are forgotten
    void Reset();
}
