#pragma once

// =============================================================================
// Toxicology - Magicka spent on poison spells uses up the copied poison
// =============================================================================
// Spec 5.6. The Magicka the player spends on poison spells (ToxData.h's cost set:
// the hostile poison spells and the poison cloaks, never a cloak's sub-spell) is
// added up; every magickaPerDose of it (core ConsumeMagicka) removes one bottle
// of the copied poison (ToxSelection.h CurrentPoison; removal in ToxBottles.h), silently, from a stack
// without the favorite hotkey first (spec 12). When the poison runs out the
// choice moves on to the next one. The running total is kept in the co-save
// (game/ToxSave.h).
//
// How the Magicka is measured: one hook on ActorMagicCaster::Update (vfunc 0x1D,
// RE/A/ActorMagicCaster.h:70-81) on the player's left and right hand casters,
// amounts kept per (spell, hand) with the caster's kDualCasting flag.
// The caster's currentSpellCost member (RE/M/MagicCaster.h:108) is the cost counted
// (spec 12: the plan's choice; GetCurrentSpellCost, RE/M/MagicCaster.h:94, is logged
// beside it once per stretch until the in-game 5 s ratio decides between them).
//   - Concentration: every frame a hand caster is in the casting state it adds
//     currentSpellCost * a_delta. While both hands cast the same spell with the
//     dual flag set, only the right hand counts (kCountOneHandWhenDual). The amounts
//     go to an SKSE task at most every 0.25 s; what a stretch leaves in its last
//     0.25 s is charged on the next frame of a player hand caster after that.
//   - Fire-and-forget: the hook remembers per hand the caster's currentSpellCost
//     (zero included); the player's TESSpellCastEvent (only sent for a released
//     cast) charges the remembered cost of that spell. A cast no hand reading
//     matches (the instant caster: scripts, the self test) is not charged. Every
//     event is charged, except a second one for the same spell within 50 ms of a
//     dual cast (the same release).
// Per-hand readings are logged once per cast stretch, and game/ToxCheck.h logs
// counted vs the player's Magicka drop every 5 s, for the in-game check. The hook
// only sees hand casters, so that check is made by casting by hand (README).
//
// Threads: the hook runs on whichever thread updates the player's casters (logged
// once) and the cast event arrives on any thread. The hook first reads an atomic
// "tracking" flag (PublishMagickaTracking, main thread) and the cost set's atomic
// snapshot (ToxData.h PoisonCostSnapshot); only a player hand caster holding a
// poison spell takes the small mutex-guarded struct (amounts, remembered costs).
// Drain tasks carry the session generation and are dropped across a load. The
// poison sets, the inventory, the selection and the running total are used on the
// game's main thread only (tasks, the messaging listener, the co-save callbacks).
// Code: game/ToxMagicka.cpp (total, cast event), game/ToxMagickaHook.cpp (hook,
// drain task), game/ToxMagickaShared.h between them.
// =============================================================================

namespace ToxGame
{
    // Adds a_amount of Magicka spent on a poison spell (main thread). Counted only while there is a
    // poison to copy (CurrentPoison non-null: the ability, "enabled" on, a favorited poison). Each
    // whole magickaPerDose removes one bottle; when the copied poison runs out, OnSelectionMaybeChanged
    // moves to the next poison and the remaining bottles come from it. Doses with no poison left are
    // dropped (spec 5.6: "남은 게 없으면 멈춤").
    void AddSpentMagicka(float a_amount);

    // The Magicka counted toward the next bottle.
    float Accumulated();

    // Concentration Magicka the hook has measured but no drain task has counted yet (read under the
    // hook's lock; nothing is taken out). The co-save stores Accumulated() + this, so the last
    // stretch's leftover is not lost to a save and load (ToxSave.cpp).
    float PendingMagicka();

    // Co-save load: the saved running total (the caller checked it is finite and >= 0).
    void SetAccumulated(float a_value);

    // Co-save Revert and a new game: the running total back to 0, and the amounts and costs the hook
    // and the cast event left for tasks forgotten.
    void ResetMagicka();

    // Main thread: the hook counts only while a_abilityAndEnabled (the ability held and "enabled" on)
    // and the cost set is not empty. Called by CurrentPoison (hits, menus, loads), every player
    // cast task and the end of ToxData.h's LoadPoisonSpells (a new cost set); changes are logged.
    void PublishMagickaTracking(bool a_abilityAndEnabled);

    // Installs the ActorMagicCaster::Update hook and registers the TESSpellCastEvent sink. Call once at
    // kDataLoaded, on the main thread.
    void InstallMagickaTracking();
}
