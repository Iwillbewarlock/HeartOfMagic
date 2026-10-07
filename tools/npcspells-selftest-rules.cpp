#include "NpcSpellsCheck.h"
#include "core/NpcSpellsRules.h"
#include <cmath>
#include <fstream>
#include <sstream>
using namespace NpcSpells;
using NpcSpellsCheck::Check;

int RunRulesSelfTest()
{
    std::printf("rules\n");
    std::string err;
    const auto necro = ParseFormRef("0C969F:Skyrim.esm", err);
    Check(necro && necro->pluginLower == "skyrim.esm" && necro->local == 0x0C969F, "FormID:Plugin parses");
    const auto modRef = ParseFormRef("000D62:MyMod.esp", err);
    Check(modRef && modRef->pluginLower == "mymod.esp" && modRef->local == 0xD62, "a mod record is a FormID too");
    const auto bad = ParseFormRef("OREO_HagravenRace:Skyrim.esm", err);
    Check(!bad, "an EditorID with a plugin is rejected");
    const auto modId = ParseFormRef("MyMod_Faction", err);
    Check(!modId && !err.empty(), "a bare EditorID is rejected (FormIDs only)");

    const Rules defaults = DefaultRules();
    Check(defaults.ladder.size() == kRungCount, "default ladder has five rungs");
    double expected[5]{};
    for (const auto& rung : defaults.ladder)
        for (std::size_t t = static_cast<std::size_t>(rung.startTier); t < kTierCount; ++t) expected[t] += rung.chance;
    bool ladderOk = true;
    for (std::size_t t = 0; t < kTierCount; ++t) ladderOk = ladderOk && std::abs(expected[t] - defaults.tierTarget[t]) < 1e-9;
    Check(ladderOk, "ladder expectations equal tierTarget per tier (1.0 1.25 1.5 2.0 2.5)");
    Check(defaults.groups.size() == 5 && defaults.groups[1].name == "Vampire", "five default groups in spec order");

    std::ifstream shipped(NPCSPELLS_DATA_DIR "/HeartOfMagic_NPCSpells.json");   // compile definition, absolute path (CWD-independent)
    std::stringstream buf; buf << shipped.rdbuf();
    Rules parsed;
    Check(ParseRules(buf.str(), parsed, err), "shipped JSON parses");
    Check(parsed.groups.size() == defaults.groups.size() && parsed.necromancerClasses.size() == 1 &&
          parsed.excludeTags == defaults.excludeTags, "shipped JSON equals built-in defaults");
    Check(parsed.keywordNpc.local == 0x013794 && parsed.keywordCreature.local == 0x013795 && parsed.keywordAnimal.local == 0x013798 &&
          parsed.keywordAnimal.pluginLower == "skyrim.esm", "shipped keywords are read");
    Rules keywordsEdited;
    Check(ParseRules(R"({ "keywords": { "animal": "000801:MyMod.esp" } })", keywordsEdited, err) && keywordsEdited.keywordAnimal.local == 0x801 &&
          keywordsEdited.keywordAnimal.pluginLower == "mymod.esp" && keywordsEdited.keywordNpc.local == 0x013794, "an edited keyword replaces only that keyword");
    Rules unresolved; err.clear();
    Check(ParseRules(R"({ "beastRaces": ["0CDD84:Skyrim.esm", "SomeRace"] })", unresolved, err) && unresolved.beastRaces.size() == 1 && !err.empty(),
          "a non-FormID entry drops that entry only, the rest of the list stays (section 7)");
    Rules shortLadder; err.clear();
    Check(!ParseRules(R"({ "ladder": [ { "startTier": "novice", "below": false, "chance": 1.0 } ] })", shortLadder, err) && !err.empty(),
          "a ladder that is not five rungs is rejected");
    Rules broken;
    Check(!ParseRules("{ not json", broken, err) && !err.empty(), "malformed JSON reports an error");
    return NpcSpellsCheck::g_failed;
}
