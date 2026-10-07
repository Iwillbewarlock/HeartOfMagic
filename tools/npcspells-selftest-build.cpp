#include "NpcSpellsCheck.h"
#include "core/NpcSpellsPool.h"
#include <nlohmann/json.hpp>
#include <algorithm>
using namespace NpcSpells; using NpcSpellsCheck::Check; using json = nlohmann::json;
namespace {
    json Node(const char* pid) { return json{ { "formId", "0x00000000" }, { "persistentId", pid } }; }
    json Entry(const char* school, const char* tier, std::vector<std::string> el, std::vector<std::string> te) {
        return json{ { "school", school }, { "tier", tier }, { "elements", el }, { "techniques", te } }; }
    json SmallTree() { return json{ { "schools", { { "Destruction", { { "nodes", json::array({ Node("A.esp|0x000801"), Node("A.esp|0x000802"), Node("A.esp|0x000803"), Node("A.esp|0x000809") }) } } },
                                                   { "Conjuration", { { "nodes", json::array({ Node("A.esp|0x000804"), Node("A.esp|0x000805"), Node("A.esp|0x000806") }) } } },
                                                   { "Restoration", { { "nodes", json::array({ Node("A.esp|0x000807"), Node("A.esp|0x000808") }) } } } } } }; }
    json SmallCatalog() {
        json c; c["version"] = 1; c["spells"] = json::object();
        c["spells"]["A.esp|0x000801"] = Entry("destruction", "novice", { "fire" }, { "curse" });
        c["spells"]["A.esp|0x000802"] = Entry("destruction", "adept", { "fire" }, { "cloak" });      // excluded technique
        c["spells"]["A.esp|0x000803"] = Entry("destruction", "master", { "frost" }, {});
        c["spells"]["A.esp|0x000804"] = Entry("conjuration", "apprentice", { "undead", "flesh" }, { "control", "infuse" }); // Raise Zombie
        c["spells"]["A.esp|0x000805"] = Entry("conjuration", "adept", { "undead", "soul" }, { "summoning", "control" });    // Conjure Boneman
        c["spells"]["A.esp|0x000806"] = Entry("conjuration", "novice", { "daedra", "weapon" }, { "summoning", "transform" });
        c["spells"]["A.esp|0x000807"] = Entry("restoration", "apprentice", { "sun", "undead" }, { "fear" });               // Turn Undead: not an undead-rule spell
        c["spells"]["A.esp|0x000808"] = Entry("restoration", "novice", { "health" }, { "curing" });
        return c;   // 0x809 is in the tree, not in the catalog
    }
}
int RunBuildSelfTest()
{
    std::printf("pool\n");
    const Rules rules = DefaultRules();
    PoolStats stats;
    auto pool = BuildPool(SmallTree(), SmallCatalog(), rules, [](const std::string& id) { return id == "A.esp|0x000808"; }, stats);
    Check(stats.treeNodes == 9, "nine tree nodes seen");
    Check(stats.notInCatalog == 1, "a node missing from the catalog is counted");
    Check(stats.excludedTag == 1 && stats.excludedDistr == 1, "cloak technique and a _DISTR.ini spell are excluded");
    Check(pool.size() == 6, "six spells kept");
    auto find = [&](const char* id) { return std::find_if(pool.begin(), pool.end(), [&](const SpellFacts& s) { return s.persistentId == id; }); };
    Check(find("A.esp|0x000804")->undeadRule && find("A.esp|0x000805")->undeadRule, "Raise Zombie and Conjure Boneman are undead-rule spells");
    Check(!find("A.esp|0x000807")->undeadRule && !find("A.esp|0x000806")->undeadRule, "Turn Undead and a bound weapon are not");
    Check(find("A.esp|0x000803")->tier == Tier::Master && find("A.esp|0x000803")->school == School::Destruction, "school and tier come from the catalog");
    ApplyEngineSchool(pool, [](const std::string& id) -> std::optional<School> {
        if (id == "A.esp|0x000801") return School::Restoration;   // planted mismatch
        if (id == "A.esp|0x000806") return std::nullopt;          // plugin gone
        if (id == "A.esp|0x000804" || id == "A.esp|0x000805") return School::Conjuration;   // every other spell keeps its catalog school
        if (id == "A.esp|0x000807" || id == "A.esp|0x000808") return School::Restoration;
        return School::Destruction; }, stats);
    Check(stats.schoolMismatch == 1 && find("A.esp|0x000801")->school == School::Restoration, "the engine school replaces the catalog school and is counted");
    Check(stats.unresolved == 1 && find("A.esp|0x000806") == pool.end(), "an unresolvable spell is dropped and counted");
    json oldCatalog = SmallCatalog(); oldCatalog["version"] = 2; PoolStats s2;
    Check(BuildPool(SmallTree(), oldCatalog, rules, nullptr, s2).empty() && s2.treeNodes == 0, "another catalog version stops the pool");
    PoolStats s3;
    Check(BuildPool(json{ { "nodes", json::array() } }, SmallCatalog(), rules, nullptr, s3).empty(), "a tree without schools stops the pool");
    Rules rejecting = rules; rejecting.excludeSpells = { "A.esp|0x000803" }; PoolStats s4;
    BuildPool(SmallTree(), SmallCatalog(), rejecting, nullptr, s4);
    Check(s4.excludedList == 1, "excludeSpells drops a spell by persistentId");
    return NpcSpellsCheck::g_failed;   // Task 5 continues this function
}
