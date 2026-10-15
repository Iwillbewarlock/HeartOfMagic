#pragma once

// =============================================================================
// NPC SPELLS - THE TREE POOL RESOLVED TO GAME FORMS
// =============================================================================
// BuildPool over the tree and catalog, then every kept spell looked up in the
// load order: the engine's school replaces the catalog's (ApplyEngineSchool),
// and a spell the game does not have, or that is not a cast spell, drops out.
// =============================================================================

#include "core/NpcSpellsPool.h"
#include "tomereach/TomeReachConfig.h"

#include "PluginNames.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace NpcSpellsGame
{
    // parallel arrays: forms[i] is facts[i]'s spell. Empty when BuildPool stopped or threw (logged).
    struct ResolvedPool
    {
        std::vector<NpcSpells::SpellFacts> facts;
        std::vector<RE::SpellItem*> forms;
    };

    ResolvedPool ResolvePool(const nlohmann::json& tree, const nlohmann::json& catalog, const NpcSpells::Rules& rules,
        const TomeReach::ConfigText& distr, NpcSpells::PoolStats& stats);

    // Finds loaded plugins by the name a persistentId or a rules FormID holds (UTF-8, any case):
    // the game's lookup by raw bytes first, then the converted names of non-ASCII files (PluginNames.h)
    using PluginLookup = PluginNames::Lookup<const RE::TESFile*>;

    // The plugin file of that name when the game loaded it (a file present in Data but not loaded has compile index 0xFF), else null
    const RE::TESFile* LookupLoadedPlugin(PluginLookup& lookup, const std::string& plugin);

    // The form `local` of `plugin`, or null when the plugin is not loaded or has no such form
    RE::TESForm* LookupLocalForm(PluginLookup& lookup, const std::string& plugin, std::uint32_t local);
}
