#pragma once

// =============================================================================
// Toxicology - poison spell hits
// =============================================================================
// Spec 5.3: a TESHitEvent sink. It keeps hits whose cause is the player, whose
// source is a Spell form and whose target is an actor other than the player,
// and hands each one to an SKSE task carrying only the target's ActorHandle,
// the spell's FormID and the session generation. Spec 5.4: the task drops a
// hit from an earlier session, checks the spell is in the poison set, the
// ability, the setting, the target alive, drops a repeat of the same (target,
// spell) inside kSameHitWindowSeconds (one hit raises several events), checks
// there is a poison to copy, then calls ApplyOrRefresh (game/ToxApply.h).
//
// Threads: the sink may run on any thread. It reads the event's fields, does a
// LookupByID form type check, takes the target's handle and reads the
// generation (atomic). Everything else - the poison set (ToxData.h is main
// thread only), the target's state, the records - runs in the task, on the
// game's main thread.
// =============================================================================

#include <cstdint>

namespace ToxGame
{
    // The session generation: a task queued before a load, a new game or a co-save Revert sees a
    // different number and is dropped. Safe from any thread.
    std::uint32_t SessionGeneration();

    // Called on kPreLoadGame, kNewGame and co-save Revert.
    void BumpGeneration();

    // The task body for one kept hit (main thread). Drops it unless a_spell is in the poison set,
    // the player has the ability with "enabled" on, the target is alive and not the player, and
    // there is a poison to copy.
    void OnPoisonSpellHit(RE::ActorHandle a_target, RE::FormID a_spell);

    // Registers the TESHitEvent sink. Call once at kDataLoaded, on the main thread.
    void RegisterHitSink();
}
