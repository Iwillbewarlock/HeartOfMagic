#include "NpcSpellsCheck.h"
#include "core/NpcSpellsPool.h"
#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsSlots.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <format>
using namespace NpcSpells; using NpcSpellsCheck::Check; using json = nlohmann::json;
namespace {
    // flag 0: own tier floor; flag 1: floor of max(tier + 1, rung start)
    std::uint16_t ExpectedLevel(Tier t, bool below, Tier start) { return below ? kTierFloor[std::max(static_cast<std::size_t>(t) + 1, static_cast<std::size_t>(start))] : kTierFloor[static_cast<std::size_t>(t)]; }
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
    std::printf("build\n");
    // Group rules for the test: group index 1 = Forsworn (prefer fire..., never sun), 4 = Thalmor (never undead)
    auto pool2 = BuildPool(SmallTree(), SmallCatalog(), rules, nullptr, stats);
    auto idx = [&](const char* id) { return static_cast<std::uint32_t>(std::find_if(pool2.begin(), pool2.end(), [&](const SpellFacts& s) { return s.persistentId == id; }) - pool2.begin()); };
    Check(InSlot(pool2[idx("A.esp|0x000801")], { School::Destruction, 1, Variant::Prefer, 0, false }, rules), "a fire spell is in the Forsworn Prefer list");
    Check(!InSlot(pool2[idx("A.esp|0x000803")], { School::Destruction, 1, Variant::Prefer, 0, false }, rules), "a frost spell is not");
    Check(!InSlot(pool2[idx("A.esp|0x000807")], { School::Restoration, 1, Variant::All, 0, false }, rules), "a sun spell is kept out of every Forsworn list (never)");
    Check(!InSlot(pool2[idx("A.esp|0x000805")], { School::Conjuration, 4, Variant::All, 0, false }, rules), "Thalmor never undead removes Conjure Boneman");
    Check(InSlot(pool2[idx("A.esp|0x000805")], { School::Conjuration, 0, Variant::NecroPrefer, 0, false }, rules) && !InSlot(pool2[idx("A.esp|0x000806")], { School::Conjuration, 0, Variant::NecroPrefer, 0, false }, rules), "NecroPrefer holds undead-rule spells only (not merely preferred ones)");
    Check(InSlot(pool2[idx("A.esp|0x000806")], { School::Conjuration, 0, Variant::NoUndeadAll, 0, false }, rules) && !InSlot(pool2[idx("A.esp|0x000804")], { School::Conjuration, 0, Variant::NoUndeadAll, 0, false }, rules), "NoUndeadAll excludes undead-rule spells");
    Check(!InSlot(pool2[idx("A.esp|0x000801")], { School::Destruction, 0, Variant::All, 4, false }, rules) && InSlot(pool2[idx("A.esp|0x000803")], { School::Destruction, 0, Variant::All, 4, false }, rules), "rung 5 (master) holds master spells only");
    Check(InSlot(pool2[idx("A.esp|0x000808")], { School::Restoration, 0, Variant::All, 0, true }, rules) && !InSlot(pool2[idx("A.esp|0x000807")], { School::Restoration, 0, Variant::All, 0, true }, rules), "non-mage list: novice curing only");

    BuildStats bs; auto lists = BuildLists(pool2, rules, bs);
    bool invariants = true;
    for (const auto& l : lists) {
        invariants = invariants && !l.entries.empty() && l.entries.size() <= kMaxEntries && (l.flags == 0 || l.flags == kAllLevelsFlag);
        const School school = pool2[l.entries.front().spellIndex].school;
        for (std::size_t i = 0; i < l.entries.size(); ++i) {
            const auto& e = l.entries[i];
            invariants = invariants && pool2[e.spellIndex].school == school && e.level == ExpectedLevel(pool2[e.spellIndex].tier, l.flags == kAllLevelsFlag, rules.ladder[SlotTable()[l.slotIndex].key.rung].startTier) && (i == 0 || l.entries[i - 1].level <= e.level);
            const auto& rung = rules.ladder[SlotTable()[l.slotIndex].key.rung];
            invariants = invariants && (SlotTable()[l.slotIndex].key.nonMage || rung.below || pool2[e.spellIndex].tier >= rung.startTier);
            invariants = invariants && (l.flags == (SlotTable()[l.slotIndex].key.nonMage ? 0 : (rung.below ? kAllLevelsFlag : 0)));
        }
    }
    Check(invariants, "every list: one school, ascending levels, level per flag, <= 255, tier >= rung start (flag 0), flag per rung");
    Check(bs.emptySlots > 0, "slots without spells are counted (small pool)");
    { const auto small = *FindSlot({ School::Destruction, kGroupNone, Variant::All, 0, false }); std::size_t n = 0; bool chunk0 = true;
      for (const auto& l : lists) if (l.slotIndex == small) { ++n; chunk0 = chunk0 && l.chunk == 0; }
      Check(n == 1 && chunk0, "a slot under 255 spells builds one list (chunk 0), never split"); }

    // 700 novice..master spells in one school -> 3 chunks, every chunk holds every tier
    std::vector<SpellFacts> big;
    for (int i = 0; i < 700; ++i) { SpellFacts f; f.persistentId = std::format("B.esp|0x{:06X}", 0x800 + i); f.school = School::Illusion; f.tier = static_cast<Tier>(i % 5); big.push_back(f); }
    BuildStats bigStats; auto bigLists = BuildLists(big, rules, bigStats);
    const auto slot = *FindSlot({ School::Illusion, kGroupNone, Variant::All, 0, false });
    std::size_t chunks = 0; bool coverage = true, cap = true;
    for (const auto& l : bigLists) if (l.slotIndex == slot) {
        ++chunks; cap = cap && l.entries.size() <= kMaxEntries;
        std::array<bool, kTierCount> seen{}; for (const auto& e : l.entries) seen[static_cast<std::size_t>(big[e.spellIndex].tier)] = true;
        for (bool b : seen) coverage = coverage && b;
    }
    Check(chunks == 3 && cap, "700 spells split into 3 chunks of <= 255");
    Check(coverage, "every chunk holds every tier (round-robin per tier)");
    Check(bigStats.droppedOverCapacity == 0, "nothing dropped at 700");
    for (int i = 700; i < 800; ++i) { SpellFacts f; f.persistentId = std::format("B.esp|0x{:06X}", 0x800 + i); f.school = School::Illusion; f.tier = Tier::Novice; big.push_back(f); }
    BuildStats overStats; BuildLists(big, rules, overStats);
    Check(overStats.droppedOverCapacity > 0, "800 spells: the surplus over 3 x 255 is dropped and counted");
    // Flag-1 (below) rung: expert rung of the None group. NPC skill picks entries with level <= skill.
    { SpellFacts f; f.school = School::Destruction;
      std::vector<SpellFacts> p; for (int i = 0; i < 5; ++i) { f.persistentId = std::format("C.esp|0x{:06X}", 0x800 + i); f.tier = static_cast<Tier>(i); p.push_back(f); }
      BuildStats cs; auto cl = BuildLists(p, rules, cs);
      const auto es = *FindSlot({ School::Destruction, kGroupNone, Variant::All, 3, false });
      const BuiltList* L = nullptr; for (const auto& l : cl) if (l.slotIndex == es) L = &l;
      auto reach = [&](int skill) { std::array<bool, kTierCount> r{}; if (L) for (const auto& e : L->entries) if (e.level <= skill) r[static_cast<std::size_t>(p[e.spellIndex].tier)] = true; return r; };
      const auto expertNpc = reach(90), masterNpc = reach(100);
      Check(L && L->flags == kAllLevelsFlag && L->entries.size() == 4, "expert rung: flag 1, master spell left out");
      Check(!expertNpc[3] && expertNpc[0] && expertNpc[1] && expertNpc[2] && !expertNpc[4], "expert-skill NPC draws novice..adept, no expert spell");
      Check(masterNpc[3] && L && L->entries.front().level == kTierFloor[3], "master-skill NPC also draws expert spells; novice..adept sit at level 75"); }
    // 3-chunk slot with a thin tier (1 master spell): every chunk keeps it
    { std::vector<SpellFacts> p; for (int i = 0; i < 700; ++i) { SpellFacts f; f.persistentId = std::format("D.esp|0x{:06X}", 0x800 + i); f.school = School::Illusion; f.tier = i == 0 ? Tier::Master : Tier::Novice; p.push_back(f); }
      BuildStats ts; auto tl = BuildLists(p, rules, ts);
      const auto is = *FindSlot({ School::Illusion, kGroupNone, Variant::All, 0, false });
      std::size_t n = 0, withMaster = 0; for (const auto& l : tl) if (l.slotIndex == is) { ++n; for (const auto& e : l.entries) if (p[e.spellIndex].tier == Tier::Master) ++withMaster; }
      Check(n == 3 && withMaster == 3, "a 1-spell tier is copied into every chunk of a 3-chunk slot"); }
    return NpcSpellsCheck::g_failed;
}
