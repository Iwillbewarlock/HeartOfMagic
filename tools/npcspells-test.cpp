// npcspells-test  -  offline harness for the NPC Spells addon's RE-free core
//   npcspells-test [--self-test]            all suites, exit 1 on any failure
//   npcspells-test --tree <spell_tree.json> --catalog <spell_catalog.json>   slot statistics on real files, lowest entry level per variant slot
#include "JsonFile.h"
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsPool.h"
#include <algorithm>
#include <cstdio>
#include <string>
namespace {
    constexpr std::size_t kBigListReport = 200;   // slots with at least this many entries are listed

    int RunRealFiles(const std::string& treePath, const std::string& catalogPath)
    {
        using namespace NpcSpells;
        const Rules rules = DefaultRules();
        PoolStats ps;
        const auto pool = BuildPool(ReadJsonFile(treePath), ReadJsonFile(catalogPath), rules, nullptr, ps);
        BuildStats bs;
        const auto lists = BuildLists(pool, rules, bs);
        std::printf("pool: %zu kept of %zu tree nodes (duplicates %zu, notInCatalog %zu, noSchoolOrTier %zu, excludedTag %zu, excludedList %zu)\n",
            pool.size(), ps.treeNodes, ps.duplicates, ps.notInCatalog, ps.noSchoolOrTier, ps.excludedTag, ps.excludedList);
        std::array<std::size_t, kSchoolCount> biggest{};
        for (const auto& l : lists) {
            auto& b = biggest[static_cast<std::size_t>(SlotTable()[l.slotIndex].key.school)];
            b = std::max(b, l.entries.size());
        }
        for (std::size_t s = 0; s < kSchoolCount; ++s)
            std::printf("%-12s lists %zu, biggest list %zu\n", std::string(kSchoolNames[s]).c_str(), bs.perSchool[s], biggest[s]);
        std::printf("lists %zu, emptySlots %zu, droppedOverCapacity %zu, variant lists topped up from All %zu\n", bs.lists, bs.emptySlots, bs.droppedOverCapacity, bs.variantsToppedUp);
        for (const auto& l : lists)
            if (l.entries.size() >= kBigListReport)
                std::printf("  big: %s (%zu entries)\n", EditorIdFor(SlotTable()[l.slotIndex].key, l.chunk).c_str(), l.entries.size());
        // Lowest entry level per slot (over its chunks): a variant slot must start no higher than its All slot, or an NPC
        // whose skill the All list serves would draw nothing from the variant
        constexpr int kNoEntry = -1;
        std::vector<int> lowest(SlotTable().size(), kNoEntry);
        for (const auto& l : lists) {
            auto& low = lowest[l.slotIndex];
            if (low == kNoEntry || l.entries.front().level < low) low = l.entries.front().level;
        }
        std::size_t variantSlots = 0, higherThanAll = 0;
        std::printf("variant slots, lowest entry level (All slot of the same school/group/rung):\n");
        for (std::size_t s = 0; s < SlotTable().size(); ++s) {
            const auto& key = SlotTable()[s].key;
            if (key.nonMage || key.variant == Variant::All) continue;
            SlotKey allKey = key; allKey.variant = Variant::All;
            const auto allSlot = FindSlot(allKey);
            const int all = allSlot ? lowest[*allSlot] : kNoEntry;
            const bool higher = all != kNoEntry && (lowest[s] == kNoEntry || lowest[s] > all);
            ++variantSlots; higherThanAll += higher ? 1 : 0;
            std::printf("  %-44s %4d (%d)%s\n", SlotTable()[s].editorId.c_str(), lowest[s], all, higher ? "  HIGHER THAN ALL" : "");
        }
        std::printf("variant slots %zu, starting higher than their All slot %zu (-1 = no list)\n", variantSlots, higherThanAll);
        return 0;   // informational: a slot that never holds undead-rule spells starts higher by design when its All tier holds only those
    }
}
int main(int argc, char** argv)
{
    std::string tree, catalog;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) tree = argv[++i];
        else if (a == "--catalog" && i + 1 < argc) catalog = argv[++i];
    }
    if (!tree.empty() && !catalog.empty()) return RunRealFiles(tree, catalog);
    RunRulesSelfTest();
    RunBuildSelfTest();
    RunNpcSelfTest();
    RunEslSelfTest();
    RunDistrSelfTest();
    std::printf("\n%d passed, %d failed\n", NpcSpellsCheck::g_passed, NpcSpellsCheck::g_failed);
    return NpcSpellsCheck::g_failed == 0 ? 0 : 1;
}
