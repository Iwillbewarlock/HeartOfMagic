#pragma once

// =============================================================================
// NPC SPELLS - INPUT FILES
// =============================================================================
// The tree and catalog Heart of Magic wrote, the addon's rules file and the
// SPID distribution configs, read once at kDataLoaded.
// =============================================================================

#include "core/NpcSpellsRules.h"
#include "tomereach/TomeReachConfig.h"

#include <nlohmann/json.hpp>
#include <string>

namespace NpcSpellsGame
{
    inline constexpr const char* kTreePath = "Data/SKSE/Plugins/SpellLearning/spell_tree.json";  // UIManagerCore.cpp:28
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    inline constexpr const char* kRulesPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells.json";
    inline constexpr const char* kReportPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells_report.txt";
    inline constexpr const char* kDataDir = "Data";

    struct Inputs
    {
        nlohmann::json tree, catalog;
        NpcSpells::Rules rules;
        bool rulesFromDefaults = false;
        std::string rulesError;
        TomeReach::ConfigText distr;  // the *_DISTR.ini (SPID) files only
        bool ok = false;
        std::string stopReason;
    };

    Inputs ReadInputs();
}
