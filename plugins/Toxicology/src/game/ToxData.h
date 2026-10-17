#pragma once

// =============================================================================
// Toxicology - game data read at load
// =============================================================================
// The settings file, the poison spell set built from the librarian's catalog,
// the ESL's forms and the tome put into Elgrim's Elixirs' chest.
//
// Threads: everything here is called from the game's main thread - the SKSE
// messaging listener (kDataLoaded, kPostLoadGame, kNewGame) and SKSE tasks
// (the hit and cast tasks). Nothing is read off that thread, so there is no lock;
// a caller on another thread would need one first. The one exception is
// PoisonCostSnapshot (below).
// =============================================================================

#include "core/ToxCore.h"

#include <memory>
#include <unordered_set>

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

    // The poison spell sets (core BuildPoisonSets), from every catalog spell tagged "poison" and
    // the spell each of its cloak effects casts (one level), counting only spells with at least
    // one hostile effect (EffectSetting::IsHostile):
    //   - hit set: the hostile catalog spells and the hostile cloak sub-spells (what reaches a target);
    //   - cost set: the hostile catalog spells and the cloaks whose sub-spell is hostile (what the
    //     player casts and pays Magicka for; a cloak's sub-spell is never charged).
    // A missing catalog or a version other than 1 leaves both empty. Runs at kDataLoaded,
    // kPostLoadGame and kNewGame (an in-game rescan rewrites the catalog).
    void LoadPoisonSpells();

    // For the hit and cast tasks: at most once every kCatalogCheckSeconds it reads the catalog
    // file's last-write time and, when it differs from the one LoadPoisonSpells last read, calls
    // LoadPoisonSpells. Returns true when it reloaded. Cheap to call on every hit.
    // Contract: the hit and cast paths call this before every IsPoisonSpell / IsPoisonCostSpell
    // check. On a fresh install Heart of Magic writes the catalog after kDataLoaded (its first full
    // scan), so until the next save load this is the only path that fills the sets.
    inline constexpr double kCatalogCheckSeconds = 5.0;
    bool ReloadPoisonSpellsIfChanged();

    // Whether a_spell is in the hit set. Call ReloadPoisonSpellsIfChanged() first (above).
    bool IsPoisonSpell(RE::FormID a_spell);

    // Whether a_spell is in the cost set. Call ReloadPoisonSpellsIfChanged() first (above).
    bool IsPoisonCostSpell(RE::FormID a_spell);

    // The cost set as LoadPoisonSpells last published it, never null. The one call here that is
    // safe on any thread (an atomic shared_ptr, replaced whole and never changed after): the
    // caster update hook filters with it before it takes its lock.
    std::shared_ptr<const std::unordered_set<RE::FormID>> PoisonCostSnapshot();

    // Puts one tome into Elgrim's Elixirs' merchant chest (base container, in memory) unless
    // it already holds one. Call once at kDataLoaded, after LoadForms.
    void PutTomeInShop();
}
