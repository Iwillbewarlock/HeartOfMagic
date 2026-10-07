#include "NpcSpellsCheck.h"
#include "core/NpcSpellsRules.h"
#include <cmath>
#include <fstream>
#include <sstream>
using namespace NpcSpells;
using NpcSpellsCheck::Check;

namespace {
    bool SameRef(const FormRef& a, const FormRef& b) { return a.pluginLower == b.pluginLower && a.local == b.local; }
    bool SameRefs(const std::vector<FormRef>& a, const std::vector<FormRef>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) if (!SameRef(a[i], b[i])) return false;
        return true;
    }
    bool SameRules(const Rules& a, const Rules& b)
    {
        if (a.tierTarget != b.tierTarget || a.ladder.size() != b.ladder.size() || a.groups.size() != b.groups.size()) return false;
        for (std::size_t i = 0; i < a.ladder.size(); ++i)
            if (a.ladder[i].startTier != b.ladder[i].startTier || a.ladder[i].below != b.ladder[i].below || std::abs(a.ladder[i].chance - b.ladder[i].chance) > 1e-9) return false;
        for (std::size_t i = 0; i < a.groups.size(); ++i) {
            const GroupRule& x = a.groups[i]; const GroupRule& y = b.groups[i];
            if (x.name != y.name || !SameRefs(x.factions, y.factions) || x.prefer != y.prefer || x.never != y.never || std::abs(x.preferShare - y.preferShare) > 1e-9) return false;
        }
        return std::abs(a.hybridScale - b.hybridScale) < 1e-9 && std::abs(a.nonMageChance - b.nonMageChance) < 1e-9 && a.nonMageTags == b.nonMageTags &&
               a.mageMinMagicShareTenths == b.mageMinMagicShareTenths && a.hybridMinMagicShareTenths == b.hybridMinMagicShareTenths &&
               SameRefs(a.necromancerClasses, b.necromancerClasses) && std::abs(a.conjurationShare - b.conjurationShare) < 1e-9 &&
               a.excludeTags == b.excludeTags && a.excludeSpells == b.excludeSpells && SameRefs(a.beastRaces, b.beastRaces) &&
               SameRefs(a.undeadExceptNpcs, b.undeadExceptNpcs) && SameRef(a.redguardRace, b.redguardRace) &&
               SameRef(a.keywordNpc, b.keywordNpc) && SameRef(a.keywordCreature, b.keywordCreature) && SameRef(a.keywordAnimal, b.keywordAnimal);
    }
}

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
    err.clear();
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
    Check(SameRules(parsed, defaults), "shipped JSON equals built-in defaults (every field)");
    Check(parsed.keywordNpc.local == 0x013794 && parsed.keywordCreature.local == 0x013795 && parsed.keywordAnimal.local == 0x013798 &&
          parsed.keywordAnimal.pluginLower == "skyrim.esm", "shipped keywords are read");
    Rules keywordsEdited; err.clear();
    Check(ParseRules(R"({ "keywords": { "animal": "000801:MyMod.esp" } })", keywordsEdited, err) && keywordsEdited.keywordAnimal.local == 0x801 &&
          keywordsEdited.keywordAnimal.pluginLower == "mymod.esp" && keywordsEdited.keywordNpc.local == 0x013794, "an edited keyword replaces only that keyword");
    Rules unresolved; err.clear();
    Check(ParseRules(R"({ "beastRaces": ["0CDD84:Skyrim.esm", "SomeRace"] })", unresolved, err) && unresolved.beastRaces.size() == 1 && !err.empty(),
          "a non-FormID entry drops that entry only, the rest of the list stays (section 7)");
    Rules shortLadder; err.clear();
    Check(!ParseRules(R"({ "ladder": [ { "startTier": "novice", "below": false, "chance": 1.0 } ] })", shortLadder, err) && !err.empty(),
          "a ladder that is not five rungs is rejected");
    Rules broken; err.clear();
    Check(!ParseRules("{ not json", broken, err) && !err.empty(), "malformed JSON reports an error");
    const char* wrongTyped[] = { R"({ "hybridScale": "x" })", R"({ "beastRaces": [5] })", R"({ "nonMageTags": 3 })", R"({ "ladder": [5,5,5,5,5] })", R"({ "groups": [5] })" };
    for (const char* text : wrongTyped) {
        Rules wrong; err.clear(); bool ok = true;
        try { ok = ParseRules(text, wrong, err); } catch (...) { ok = true; err = "threw"; }
        Check(!ok && !err.empty() && err != "threw", text);
    }
    return NpcSpellsCheck::g_failed;
}
