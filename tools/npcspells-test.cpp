// npcspells-test  -  offline harness for the NPC Spells addon's RE-free core
//   npcspells-test [--self-test]            all suites, exit 1 on any failure
//   npcspells-test --tree <spell_tree.json> --catalog <spell_catalog.json>   slot statistics on real files
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
        std::printf("pool: %zu kept of %zu tree nodes (notInCatalog %zu, noSchoolOrTier %zu, excludedTag %zu, excludedList %zu)\n",
            pool.size(), ps.treeNodes, ps.notInCatalog, ps.noSchoolOrTier, ps.excludedTag, ps.excludedList);
        std::array<std::size_t, kSchoolCount> biggest{};
        for (const auto& l : lists) {
            auto& b = biggest[static_cast<std::size_t>(SlotTable()[l.slotIndex].key.school)];
            b = std::max(b, l.entries.size());
        }
        for (std::size_t s = 0; s < kSchoolCount; ++s)
            std::printf("%-12s lists %zu, biggest list %zu\n", std::string(kSchoolNames[s]).c_str(), bs.perSchool[s], biggest[s]);
        std::printf("lists %zu, emptySlots %zu, droppedOverCapacity %zu\n", bs.lists, bs.emptySlots, bs.droppedOverCapacity);
        for (const auto& l : lists)
            if (l.entries.size() >= kBigListReport)
                std::printf("  big: %s (%zu entries)\n", EditorIdFor(SlotTable()[l.slotIndex].key, l.chunk).c_str(), l.entries.size());
        return 0;
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
    std::printf("\n%d passed, %d failed\n", NpcSpellsCheck::g_passed, NpcSpellsCheck::g_failed);
    return NpcSpellsCheck::g_failed == 0 ? 0 : 1;
}
