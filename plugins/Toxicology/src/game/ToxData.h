#pragma once

// =============================================================================
// Toxicology - game data read at load
// =============================================================================
// The settings file, the poison spell set built from the librarian's catalog,
// the ESL's forms and the tome put into Elgrim's Elixirs' chest.
//
// Threads: everything here is called from the game's main thread - the SKSE
// messaging listener (kDataLoaded, kPostLoadGame, kNewGame) and SKSE tasks
// (the hit task). Nothing is read off that thread, so there is no lock; a
// caller on another thread would need one first.
// =============================================================================

#include "core/ToxCore.h"

namespace ToxGame
{
    inline constexpr const char* kSettingsPath = "Data/SKSE/Plugins/HeartOfMagic_Toxicology.json";
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    inline constexpr const char* kEslName = "HeartOfMagic_Toxicology.esp";

    // The settings LoadSettings read (defaults before it runs or when the file is missing or broken).
    const Toxicology::Settings& GetSettings();

    // Reads kSettingsPath once; a missing file or a bad value keeps the default and is logged.
    void LoadSettings();

    // The ESL's keyword, ability and tome (local FormIDs 0x800, 0x802, 0x803), looked up by
    // LoadForms at kDataLoaded. Null when the ESL is not loaded.
    RE::BGSKeyword* MimicKeyword();
    RE::SpellItem* Ability();
    RE::TESObjectBOOK* Tome();

    // Looks the ESL's forms up and logs whether the ESL is loaded. Call once at kDataLoaded.
    void LoadForms();

    // True when the player has the Toxicology ability (false without the ESL).
    bool PlayerHasAbility();

    // The poison spell set: every catalog spell tagged "poison", plus the spell each of its
    // cloak effects casts (one level), keeping only spells with at least one hostile effect
    // (EffectSetting::IsHostile). A missing catalog or a version other than 1 leaves it empty.
    // Runs at kDataLoaded, kPostLoadGame and kNewGame (an in-game rescan rewrites the catalog).
    void LoadPoisonSpells();

    // For the hit task: at most once every kCatalogCheckSeconds it reads the catalog file's
    // last-write time and, when it differs from the one LoadPoisonSpells last read, calls
    // LoadPoisonSpells. Returns true when it reloaded. Cheap to call on every hit.
    inline constexpr double kCatalogCheckSeconds = 5.0;
    bool ReloadPoisonSpellsIfChanged();

    bool IsPoisonSpell(RE::FormID a_spell);

    // Puts one tome into Elgrim's Elixirs' merchant chest (base container, in memory) unless
    // it already holds one. Call once at kDataLoaded, after LoadForms.
    void PutTomeInShop();
}
