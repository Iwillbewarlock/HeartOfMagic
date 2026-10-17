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
// While the self test switches it on, each spell's hits are traced (HitTrace).
//
// Threads: the sink may run on any thread. It reads the event's fields, does a
// LookupByID form type check, takes the target's handle and reads the
// generation (atomic). Everything else - the poison set (ToxData.h is main
// thread only), the target's state, the records - runs in the task, on the
// game's main thread.
// =============================================================================

#include "game/ToxApply.h"

#include <cstdint>
#include <optional>

namespace ToxGame
{
    // =========================================================================
    // Hit trace (the dev-only self test, game/ToxSelfTest.h)
    // =========================================================================

    // How far the last hit task of a spell got
    enum class HitStage
    {
        kNone,            // no task ran for it yet
        kOff,             // "enabled" off or no ability
        kNotPoisonSpell,  // not in the poison set
        kTargetGone,      // no player, or the target is gone, dead or the player
        kSameHit,         // a repeat of the same hit inside kSameHitWindowSeconds
        kNoPoison,        // no poison to copy
        kApplied,         // ApplyOrRefresh ran; HitTrace::outcome holds what it did
    };

    // What became of one spell's hits since tracing was switched on
    struct HitTrace
    {
        std::uint32_t rawEvents = 0;  // TESHitEvents with this source, before any check
        std::uint32_t sinkHits = 0;   // events the sink kept (cause the player, target another actor)
        std::uint32_t tasks = 0;      // hit tasks that ran
        std::uint32_t applied = 0;    // tasks that reached ApplyOrRefresh
        HitStage last = HitStage::kNone;
        std::optional<ApplyOutcome> outcome;  // the last ApplyOrRefresh result
    };

    // Main thread. On: clears every trace and starts counting; off: stops (and clears). Off by default:
    // outside the self test the sink only reads one atomic flag for it.
    void SetHitTracing(bool a_on);

    // Main thread. The trace of a_spell (zeros when none).
    HitTrace HitTraceFor(RE::FormID a_spell);

    // =========================================================================
    // Hits
    // =========================================================================

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
