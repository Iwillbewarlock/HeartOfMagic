#pragma once

// =============================================================================
// Toxicology - putting the copied poison on a target, or refreshing it
// =============================================================================
// Spec 5.4. Per (target FormID, poison FormID): the last time we acted and the
// ActiveEffects our own cast created. A hit inside minIntervalSeconds of the
// last action does nothing. Otherwise: our effects still on the target get
// elapsedSeconds = 0; if none is left and the target carries the same poison
// from someone else (a coated weapon, an effect from before the load) nothing
// happens; if neither, the poison is cast on the target and the effects it
// created are remembered. Effects flagged kInactive or kDispelled count as gone
// (spec 12). The records are never saved: after a load every effect is foreign.
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
    enum class ApplyOutcome
    {
        kTooSoon,         // inside minIntervalSeconds of the last action on (target, poison)
        kRefreshed,       // our effects still on the target were restarted
        kForeignPresent,  // the same poison is on the target but not from our cast: nothing done
        kCast,            // cast, and the new effects were recorded
        kCastNoEffect,    // cast, but no new effect of ours showed up in the target's list
        kNoCaster,        // the caster's instant MagicCaster was missing
    };

    // Spec 5.4 for one hit. a_target, a_poison and a_player are non-null; the caller checked the
    // ability, the setting, that the target is alive and not the player, and that the poison is
    // the current choice.
    ApplyOutcome ApplyOrRefresh(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player);

    // Forgets every record and last-action time. Called on kPreLoadGame, kNewGame and co-save Revert.
    void ClearApplications();

    // The actor's active effect list, or null. Always through Actor::AsMagicTarget(): the MagicTarget
    // base sits at 0x98 before 1.6.629 and 0xA0 from it (RE/A/Actor.h:777), so calling
    // Actor::GetActiveEffectList() directly uses the compile-time offset and, on the other runtime,
    // calls through the wrong vtable (crash 2026-10-17: RIP 0 in ApplyOrRefresh on 1.6.1170).
    RE::BSSimpleList<RE::ActiveEffect*>* ActiveEffectsOf(RE::Actor* a_actor);
}
