#include "NpcSpellsCheck.h"
#include "core/NpcSpellsRules.h"
#include "core/NpcSpellsClassTier.h"
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
        if (a.ladder.size() != b.ladder.size() || a.groups.size() != b.groups.size()) return false;
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
    // The ladder is the only knob for spells per tier: sum of chances over rungs that reach the tier
    constexpr std::array<double, kTierCount> kLadderExpectation{ 1.0, 1.25, 1.5, 2.0, 2.5 };
    std::array<double, kTierCount> expected{};
    for (const auto& rung : defaults.ladder)
        for (std::size_t t = static_cast<std::size_t>(rung.startTier); t < kTierCount; ++t) expected[t] += rung.chance;
    bool ladderOk = true;
    for (std::size_t t = 0; t < kTierCount; ++t) ladderOk = ladderOk && std::abs(expected[t] - kLadderExpectation[t]) < 1e-9;
    Check(ladderOk, "default ladder gives 1.0 1.25 1.5 2.0 2.5 spells per school by tier");
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
    Rules nonString; err.clear();
    Check(ParseRules(R"({ "beastRaces": ["0CDD84:Skyrim.esm", 5, "SomeRace", "00283A:Dawnguard.esm"] })", nonString, err) && nonString.beastRaces.size() == 2 &&
          nonString.beastRaces[1].pluginLower == "dawnguard.esm" && err.find("5") != std::string::npos && err.find("SomeRace") != std::string::npos && err.find("; ") != std::string::npos,
          "a non-string entry drops that entry only, and every bad entry is named (errors kept, not overwritten)");
    Rules shippedClean; err.clear();
    Check(ParseRules(buf.str(), shippedClean, err) && err.empty(), "shipped JSON parses without an error");
    Rules outOfRange; err.clear();
    Check(ParseRules(R"({ "ladder": [ { "startTier": "novice", "below": false, "chance": 1.0 }, { "startTier": "apprentice", "below": true, "chance": 1.5 },
                          { "startTier": "adept", "below": true, "chance": 0.25 }, { "startTier": "expert", "below": true, "chance": 0.5 }, { "startTier": "master", "below": false, "chance": 0.5 } ],
                          "hybridScale": 1.5, "nonMageChance": -0.1, "conjuration": { "share": 2 }, "groups": [ { "name": "Thalmor", "preferShare": 1.2 } ] })", outOfRange, err) &&
          outOfRange.ladder[1].chance == defaults.ladder[1].chance && outOfRange.hybridScale == defaults.hybridScale && outOfRange.nonMageChance == defaults.nonMageChance &&
          outOfRange.conjurationShare == defaults.conjurationShare && outOfRange.groups[3].preferShare == defaults.groups[3].preferShare &&
          err.find("ladder[1].chance") != std::string::npos && err.find("hybridScale") != std::string::npos && err.find("nonMageChance") != std::string::npos &&
          err.find("conjuration.share") != std::string::npos && err.find("groups.Thalmor.preferShare") != std::string::npos,
          "a chance, share or scale outside 0..1 goes back to its default, each one named");
    Rules edges; err.clear();
    Check(ParseRules(R"({ "hybridScale": 0, "nonMageChance": 1, "conjuration": { "share": 1.0 } })", edges, err) && err.empty() && edges.hybridScale == 0.0 &&
          edges.nonMageChance == 1.0 && edges.conjurationShare == 1.0, "0 and 1 are inside the range (no error)");
    Rules swappedTiers; err.clear();
    Check(ParseRules(R"({ "classTiers": { "mageMinMagicShare": 0.3, "hybridMinMagicShare": 0.5 } })", swappedTiers, err) &&
          swappedTiers.mageMinMagicShareTenths == defaults.mageMinMagicShareTenths && swappedTiers.hybridMinMagicShareTenths == defaults.hybridMinMagicShareTenths &&
          err.find("hybridMinMagicShare") != std::string::npos, "hybridMinMagicShare above mageMinMagicShare puts both back to the defaults");
    Rules badMage; err.clear();
    Check(ParseRules(R"({ "classTiers": { "mageMinMagicShare": 1.4, "hybridMinMagicShare": 0.2 } })", badMage, err) &&
          badMage.mageMinMagicShareTenths == defaults.mageMinMagicShareTenths && badMage.hybridMinMagicShareTenths == 2 && err.find("mageMinMagicShare") != std::string::npos,
          "a class share outside 0..1 goes back to its default, the other stays");
    Rules shortLadder; err.clear();
    Check(!ParseRules(R"({ "ladder": [ { "startTier": "novice", "below": false, "chance": 1.0 } ] })", shortLadder, err) && !err.empty(),
          "a ladder that is not five rungs is rejected");
    Rules broken; err.clear();
    Check(!ParseRules("{ not json", broken, err) && !err.empty(), "malformed JSON reports an error");
    const char* wrongTyped[] = { R"({ "hybridScale": "x" })", R"({ "nonMageTags": 3 })", R"({ "ladder": [5,5,5,5,5] })", R"({ "groups": [5] })" };
    for (const char* text : wrongTyped) {
        Rules wrong; err.clear(); bool ok = true;
        try { ok = ParseRules(text, wrong, err); } catch (...) { ok = true; err = "threw"; }
        Check(!ok && !err.empty() && err != "threw", text);
    }
    auto W = [](std::initializer_list<int> magic, int nonMagicSum) { ClassWeights c; int i = 0; for (int m : magic) c.w[kFirstMagicSkill + i++] = static_cast<std::uint8_t>(m); c.w[0] = static_cast<std::uint8_t>(nonMagicSum); return c; };
    // Skyrim.esm values (ledger): CombatMageConjurer alt1 conj3 des2 ill1 res3 = 10 of 11 -> .909
    Check(ClassifyClass(W({ 1, 3, 2, 1, 3 }, 1), defaults) == ClassTier::Mage, "CombatMageConjurer .909 is a mage");
    Check(ClassifyClass(W({ 2, 1, 2, 1, 1 }, 4), defaults) == ClassTier::Mage, "CombatSorcerer .636 is a mage");
    Check(ClassifyClass(W({ 1, 1, 2, 1, 1 }, 5), defaults) == ClassTier::Hybrid, "CombatWitchblade .545 is hybrid");
    Check(ClassifyClass(W({ 1, 0, 1, 1, 1 }, 7), defaults) == ClassTier::Hybrid, "CombatMonk .364 is hybrid");
    Check(ClassifyClass(W({ 0, 0, 1, 1, 1 }, 8), defaults) == ClassTier::NonMage, "CombatScout .273 is a non-mage");
    Check(ClassifyClass(W({ 0, 0, 0, 0, 0 }, 9), defaults) == ClassTier::NonMage, "a warrior with no magic weight is a non-mage");
    Check(ClassifyClass(W({ 3, 0, 3, 0, 0 }, 4), defaults) == ClassTier::Mage, "exactly 0.600 counts as mage (integer compare)");
    Check(ClassifyClass(W({ 3, 0, 0, 0, 0 }, 7), defaults) == ClassTier::Hybrid, "exactly 0.300 counts as hybrid");
    Check(IsConjurationClass(W({ 1, 3, 2, 1, 3 }, 1)), "conjuration 3 = restoration 3 tie is a conjuration class");
    Check(!IsConjurationClass(W({ 1, 3, 4, 1, 1 }, 1)), "destruction above conjuration is not (conjuration 3 meets the floor)");
    Check(!IsConjurationClass(W({ 0, 2, 0, 0, 0 }, 1)), "conjuration 2 is below the floor of 3");
    const auto scope = SchoolsWeighted(W({ 1, 0, 2, 0, 1 }, 3));
    Check(scope[0] && !scope[1] && scope[2] && !scope[3] && scope[4], "weighted schools are those with weight > 0");
    return NpcSpellsCheck::g_failed;
}
