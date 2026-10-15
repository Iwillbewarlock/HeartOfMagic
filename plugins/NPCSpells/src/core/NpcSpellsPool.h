#pragma once
#include "core/NpcSpellsRules.h"
#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace NpcSpells {
    struct SpellFacts {
        std::string persistentId;           // "Plugin.esp|0x00XXXX" (catalog key)
        School school = School::Alteration; // catalog school, replaced by the engine school in ApplyEngineSchool
        Tier tier = Tier::Novice;
        std::vector<std::string> elements;  // catalog "elements"
        std::vector<std::string> techniques;// catalog "techniques"
        bool undeadRule = false;            // element "undead" and technique "summoning" or "control" (undead rule spells)
        bool HasAnyTag(const std::vector<std::string>& tags) const;
    };
    struct PoolStats { std::size_t treeNodes = 0, notInCatalog = 0, noSchoolOrTier = 0, excludedTag = 0, excludedList = 0, excludedDistr = 0, unresolved = 0, schoolMismatch = 0, kept = 0; };
    using DistributedFn = std::function<bool(const std::string& persistentId)>;   // another *_DISTR.ini (SPID) already names it
    // Stops with an empty pool and stats.treeNodes == 0 when catalog["version"] != 1 or tree has no "schools".
    std::vector<SpellFacts> BuildPool(const nlohmann::json& tree, const nlohmann::json& catalog, const Rules& rules, const DistributedFn& distributed, PoolStats& stats);
    using EngineSchoolFn = std::function<std::optional<School>(const std::string& persistentId)>;  // nullopt = form not found or not a spell
    void ApplyEngineSchool(std::vector<SpellFacts>& pool, const EngineSchoolFn& engineSchool, PoolStats& stats);
    inline constexpr int kCatalogVersion = 1;   // plugins/spelllearning/include/librarian/Librarian.h:234
}
