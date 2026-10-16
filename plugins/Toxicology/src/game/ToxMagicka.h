#pragma once

// =============================================================================
// Toxicology - Magicka spent on poison spells uses up the copied poison
// =============================================================================
// Spec 5.6. The Magicka the player spends on poison spells (ToxData.h's set) is
// added up; every magickaPerDose of it (core ConsumeMagicka) removes one bottle
// of the copied poison (ToxSelection.h CurrentPoison; removal in ToxBottles.h), silently, from a stack
// without the favorite hotkey first (spec 12). When the poison runs out the
// choice moves on to the next one. The running total is kept in the co-save
// (game/ToxSave.h).
//
// How the Magicka is measured: one hook on ActorMagicCaster::Update (vfunc 0x1D,
// RE/A/ActorMagicCaster.h:70-81) on the player's left and right hand casters.
//   - Concentration: every frame the caster is in the casting state it adds
//     GetCurrentSpellCost() * a_delta (RE/M/MagicCaster.h:94) - the caster's own
//     cost, dual casting and perks included.
//   - Fire-and-forget: the hook remembers per hand the caster's currentSpellCost;
//     the player's TESSpellCastEvent (only sent for a released cast) charges the
//     remembered cost of that spell, or MagicItem::CalculateMagickaCost (times
//     MagicFormulas::CalcDualCastCost when dual casting) when no hand has it.
//
// Threads: the hook runs on whichever thread updates the player's casters (logged
// once) and the cast event arrives on any thread. Both only touch a small
// mutex-guarded struct (amounts, remembered costs) and queue SKSE tasks; the
// poison set, the inventory, the selection and the running total are used on the
// game's main thread only (tasks, the messaging listener, the co-save callbacks).
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

    // Co-save load: the saved running total (the caller checked it is finite and >= 0).
    void SetAccumulated(float a_value);

    // Co-save Revert and a new game: the running total back to 0, and the amounts and costs the hook
    // and the cast event left for tasks forgotten.
    void ResetMagicka();

    // Installs the ActorMagicCaster::Update hook and registers the TESSpellCastEvent sink. Call once at
    // kDataLoaded, on the main thread.
    void InstallMagickaTracking();
}
