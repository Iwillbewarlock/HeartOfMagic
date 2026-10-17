#pragma once

// =============================================================================
// Toxicology - putting the copied poison on a target, once
// =============================================================================
// Spec 5.4. Per (target FormID, poison FormID) the poison is cast once per
// session: a hit for a pair already applied does nothing. A pair is recorded
// whenever the cast was made (a caster was found), even when no lasting effect
// shows up (an instant-only poison). The record is never saved: a save load, a
// new game and a co-save Revert empty it, so after a load each target can be
// poisoned once more.
//
// Threads: game main thread only - the hit task (ToxHits.cpp) and the SKSE
// messaging listener / co-save Revert callback (ClearApplications). No lock; a
// caller on another thread would need one first.
// =============================================================================

namespace RE
{
    class Actor;
    class ActiveEffect;
    class AlchemyItem;
    template <class T> class BSSimpleList;
}

namespace ToxGame
{
    // kNoCaster stays last: ToxHits.h counts the kinds as kNoCaster + 1
    enum class ApplyOutcome
    {
        kAlreadyApplied,  // the poison was already cast on this target this session: nothing done
        kCast,            // cast, and a new effect of the poison showed up on the target
        kCastNoEffect,    // cast (and recorded), but no new effect of the poison showed up on the target
        kNoCaster,        // the caster's instant MagicCaster was missing: not cast, not recorded
    };

    // Spec 5.4 for one hit. a_target, a_poison and a_player are non-null; the caller checked the
    // ability, the setting, that the target is alive and not the player, and that the poison is
    // the current choice.
    ApplyOutcome ApplyOnce(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player);

    // Forgets every applied pair. Called on kPreLoadGame, kNewGame and co-save Revert.
    void ClearApplications();

    // The actor's active effect list, or null. Always through Actor::AsMagicTarget(): the MagicTarget
    // base sits at 0x98 before 1.6.629 and 0xA0 from it (RE/A/Actor.h:777), so calling
    // Actor::GetActiveEffectList() directly uses the compile-time offset and, on the other runtime,
    // calls through the wrong vtable (crash 2026-10-17: RIP 0 in the apply step on 1.6.1170).
    RE::BSSimpleList<RE::ActiveEffect*>* ActiveEffectsOf(RE::Actor* a_actor);
}
