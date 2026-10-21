#pragma once

#include "Common.h"

// =============================================================================
// LANTERN DAMAGE - the worn lantern's extra damage on the player's fire hits (design 2026-10-21, section 5)
// =============================================================================
// HitEventSink (HitEvents.cpp, any thread) hands every TESHitEvent to OnHit: only FormID work happens there (the
// source classified once per id and cached, LanternDamageRules.h) and the per-target ICD under a mutex. An accepted
// hit queues a game-thread task: target an Actor, alive, not the player; the buff on and the player's active effect
// HoMBF_LanternEffect (0x810) the reason; then HoMBF_LanternBurnSpell (0x812, Ignore Resistance) is cast on it with
// max health x pct as its magnitude, the player blamed (kill credit), health read before and after in the same task.
// A TESDeathEvent of a target that took lantern damage in the last 2 s logs the kill. Off for the session without the
// lantern records, with lanternDamage false, or without the main-thread queue (DisableEngineWrites).

namespace BlackFlame::LanternDamage
{
    /// kDataLoaded, after LoadForms and BuildFireSets: the fire hazards' spells, the death sink
    void Init();

    /// HitEventSink::ProcessEvent (any thread; nothing throws out of the caller's try)
    void OnHit(const RE::TESHitEvent& a_event);

    /// Message handler, world torn down: the ICD and the recent damage table are forgotten
    void Reset();
}
