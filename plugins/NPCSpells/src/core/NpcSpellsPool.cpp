#include "core/NpcSpellsPool.h"
#include <algorithm>
namespace NpcSpells {
bool SpellFacts::HasAnyTag(const std::vector<std::string>& tags) const
{
    for (const auto& t : tags) if (std::find(elements.begin(), elements.end(), t) != elements.end() || std::find(techniques.begin(), techniques.end(), t) != techniques.end()) return true;
    return false;
}
std::vector<SpellFacts> BuildPool(const nlohmann::json& tree, const nlohmann::json& catalog, const Rules& rules, const DistributedFn& distributed, PoolStats& stats)
{
    std::vector<SpellFacts> pool;
    if (!catalog.is_object() || catalog.value("version", 0) != kCatalogVersion || !catalog.contains("spells")) return pool;
    const auto schools = tree.find("schools");
    if (schools == tree.end() || !schools->is_object()) return pool;
    const auto& spells = catalog["spells"];
    for (const auto& [schoolName, schoolData] : schools->items()) {
        const auto nodes = schoolData.find("nodes");
        if (nodes == schoolData.end() || !nodes->is_array()) continue;
        for (const auto& node : *nodes) {
            ++stats.treeNodes;
            const std::string pid = node.value("persistentId", "");
            const auto entry = spells.find(pid);
            if (pid.empty() || entry == spells.end()) { ++stats.notInCatalog; continue; }
            SpellFacts f; f.persistentId = pid;
            const auto school = SchoolFromName(entry->value("school", "")); const auto tier = TierFromName(entry->value("tier", ""));
            if (!school || !tier) { ++stats.noSchoolOrTier; continue; }
            f.school = *school; f.tier = *tier;
            f.elements = entry->value("elements", std::vector<std::string>{}); f.techniques = entry->value("techniques", std::vector<std::string>{});
            if (f.HasAnyTag(rules.excludeTags)) { ++stats.excludedTag; continue; }
            if (std::find(rules.excludeSpells.begin(), rules.excludeSpells.end(), pid) != rules.excludeSpells.end()) { ++stats.excludedList; continue; }
            if (distributed && distributed(pid)) { ++stats.excludedDistr; continue; }
            f.undeadRule = f.HasAnyTag({ "undead" }) && (f.HasAnyTag({ "summoning" }) || f.HasAnyTag({ "control" }));
            pool.push_back(std::move(f)); ++stats.kept;
        }
    }
    return pool;
}
void ApplyEngineSchool(std::vector<SpellFacts>& pool, const EngineSchoolFn& engineSchool, PoolStats& stats)
{
    std::vector<SpellFacts> kept; kept.reserve(pool.size());
    for (auto& f : pool) {
        const auto school = engineSchool(f.persistentId);
        if (!school) { ++stats.unresolved; continue; }
        if (*school != f.school) { ++stats.schoolMismatch; f.school = *school; }
        kept.push_back(std::move(f));
    }
    pool.swap(kept); stats.kept = pool.size();
}
}
