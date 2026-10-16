#pragma once

#include "Common.h"

// =============================================================================
// CoSave — the SKSE serialization callbacks (save, load, revert)
// =============================================================================
// One record stream, three owners: ProgressionManager (SLTR targets, SLPR
// progress), SpellEffectivenessHook (SLEL early-learned spells) and SpellTomeHook
// (SLTX tome XP once). Record layouts and versions: docs/ARCHITECTURE.md,
// "Co-save records". Registered in SKSEPluginLoad (Main.cpp).
namespace CoSave
{
    inline constexpr std::uint32_t kUniqueID = 'SPLL';  // Spell Learning

    void OnGameSaved(SKSE::SerializationInterface* a_intfc);
    void OnGameLoaded(SKSE::SerializationInterface* a_intfc);
    void OnRevert(SKSE::SerializationInterface* a_intfc);
}
