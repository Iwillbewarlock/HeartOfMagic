// npcspells-test  -  offline harness for the NPC Spells addon's RE-free core
//   npcspells-test [--self-test]            all suites, exit 1 on any failure
//   npcspells-test --tree <spell_tree.json> --catalog <spell_catalog.json>   slot statistics on real files (Task 5)
#include "NpcSpellsCheck.h"
#include <cstdio>
#include <string>
// Stub until Task 5 replaces it in this file (slot statistics on real files)
int RunRealFiles(const std::string&, const std::string&) { return 0; }
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
