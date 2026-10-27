// =============================================================================
// librarian-test --self-test  -  the classifier on small made-up spells
// =============================================================================
//
// Checks what a dump cannot pin down one case at a time: the conflict stage
// (framework keywords the record contradicts - an unspecified-damage mark, a
// different engine element on the same effect, a cloak judged by the damage it
// applies, siblings that say nothing about each other), the per-element
// evidence tiers, the axis sources after removals and their tie-break, and
// removals holding through the noElement pass. Needs no dump, no rule directory.

#include "Common.h"
#include "LibrarianSelfTest.h"

#include "librarian/Librarian.h"

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
    using json = nlohmann::json;

    int g_failed = 0;
    int g_passed = 0;

    void Check(bool ok, const char* what)
    {
        (ok ? g_passed : g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    constexpr const char* kUnspecifiedApo = "APO_MagicDamageUnspecified";
    constexpr const char* kUnspecifiedDar = "DAR_UnspecificMagicDamage";
    constexpr const char* kManualSpell = "Test.esp|0x000801";

    // Captures what the librarian logs while it lives, then puts the default
    // logger back.
    class LogCapture
    {
    public:
        LogCapture() : m_previous(spdlog::default_logger())
        {
            auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_text);
            spdlog::set_default_logger(std::make_shared<spdlog::logger>("selftest", sink));
        }
        ~LogCapture() { spdlog::set_default_logger(m_previous); }
        LogCapture(const LogCapture&) = delete;
        LogCapture& operator=(const LogCapture&) = delete;
        [[nodiscard]] std::string Text() const { return m_text.str(); }

    private:
        std::ostringstream m_text;
        std::shared_ptr<spdlog::logger> m_previous;
    };

    // The shapes of the shipped files, small: framework (10_), the record
    // (00_mgef), the conflict marks (20_), the hand-made list (80_), then two
    // user files with tier names of their own. Appended framework first, so a
    // framework rule is the first to tag what both tag.
    Librarian::RuleSet MakeRules(bool withMarks = true)
    {
        Librarian::RuleSet rules;
        Librarian::AppendRules(json::parse(R"({ "tier": "framework", "rules": [
            { "match": { "mgefKeyword": "KIT_MagicElement_Fire" },      "add": { "elements": ["fire"] } },
            { "match": { "mgefKeyword": "KIT_MagicElement_Frost" },     "add": { "elements": ["frost"] } },
            { "match": { "mgefKeyword": "KIT_MagicElement_Shock" },     "add": { "elements": ["shock"] } },
            { "match": { "mgefKeyword": "KIT_MagicElement_Poison" },    "add": { "elements": ["poison"] } },
            { "match": { "mgefKeyword": "KIT_MagicElement_Necrotic" },  "add": { "elements": ["necrotic"] } },
            { "match": { "mgefKeywordPrefix": "OCF_MgefSpellDamage_NecroticFire" },
              "add": { "elements": ["necrotic", "fire"] } },
            { "match": { "mgefKeyword": "KIT_MagicElement_AshFire" },   "add": { "elements": ["fire", "earth"] } },
            { "match": { "mgefKeywordPrefix": "ADAR_SPEL_Water_" },     "add": { "elements": ["water"] } },
            { "match": { "mgefKeyword": "KIT_TechCurse" },              "add": { "techniques": ["curse"] } },
            { "match": { "spellKeyword": "KIT_SpellFire" },             "add": { "elements": ["fire"] } }
        ] })"), "10_test.json", rules);
        Librarian::AppendRules(json::parse(R"({ "rules": [
            { "match": { "mgefKeyword": "MagicDamageFire" },  "add": { "elements": ["fire"] } },
            { "match": { "mgefKeyword": "MagicDamageFrost" }, "add": { "elements": ["frost"] } },
            { "match": { "resistance": "FireResist" },        "add": { "elements": ["fire"] } },
            { "match": { "archetype": "Cloak" },              "add": { "techniques": ["cloak"] } },
            { "match": { "archetype": "Reanimate" },          "remove": { "elements": ["fire"] } },
            { "match": { "mgefKeyword": "TestNoArcane" },     "remove": { "elements": ["arcane"] } },
            { "match": { "mgefKeyword": "MagicDamageResist", "noElement": true }, "add": { "elements": ["arcane"] } }
        ] })"), "00_test.json", rules);
        if (withMarks) {
            Librarian::AppendRules(json::parse(std::string(R"({ "unspecifiedDamageKeywords": [")")
                + kUnspecifiedApo + R"(", ")" + kUnspecifiedDar + R"(", 7, ""] })"), "20_test.json", rules);
        }
        Librarian::AppendRules(json::parse(std::string(R"({ "tier": "manual", "rules": [
            { "match": { "spell": ")") + kManualSpell + R"(" }, "add": { "elements": ["fire"] } },
            { "match": { "archetype": "PeakValueModifier", "primaryAV": "FireResist", "hostile": false },
              "add": { "elements": ["fire", "resistance"] } }
        ] })"), "80_test.json", rules);
        Librarian::AppendRules(json::parse(R"({ "tier": "zeta", "rules": [
            { "match": { "mgefKeyword": "ZetaWater" }, "add": { "elements": ["water"] } },
            { "match": { "mgefKeyword": "UserFire" },  "add": { "elements": ["fire"] } }
        ] })"), "90_test.json", rules);
        Librarian::AppendRules(json::parse(R"({ "tier": "alpha", "rules": [
            { "match": { "mgefKeyword": "AlphaAir" }, "add": { "elements": ["air"] } }
        ] })"), "91_test.json", rules);
        return rules;
    }

    json Effect(std::vector<std::string> keywords, const char* resistance = "MagicResist",
        const char* archetype = "ValueModifier")
    {
        return { { "archetype", archetype }, { "resistance", resistance }, { "primaryAV", "Health" },
            { "hostile", true }, { "detrimental", true }, { "magnitude", 10 },
            { "flags", { { "hideInUI", false } } }, { "keywords", keywords } };
    }

    json HiddenHelper(std::vector<std::string> keywords)
    {
        return { { "archetype", "Script" }, { "resistance", "None" }, { "detrimental", false },
            { "magnitude", 0 }, { "flags", { { "hideInUI", true } } }, { "keywords", keywords } };
    }

    // A cloak body (deals no damage itself) and what it applies to others.
    json Cloak(std::vector<std::string> keywords, json applied)
    {
        json body = Effect(std::move(keywords), "MagicResist", "Cloak");
        body["applies"] = json::array({ { { "effects", std::move(applied) } } });
        return body;
    }

    json Spell(json effects, const char* id = "Test.esp|0x000800", std::vector<std::string> keywords = {})
    {
        return { { "persistentId", id }, { "effects", std::move(effects) }, { "keywords", keywords } };
    }

    bool Has(const Librarian::TagSet& tags, const char* element) { return tags.elements.contains(element); }

    bool Dropped(const Librarian::TagSet& tags, const char* element)
    {
        return std::find(tags.conflictDropped.begin(), tags.conflictDropped.end(), element) != tags.conflictDropped.end();
    }

    std::string SourceOf(const Librarian::TagSet& tags, const char* element)
    {
        const auto found = tags.elementSources.find(element);
        return found == tags.elementSources.end() ? std::string{} : found->second;
    }

    void ConflictStage(const Librarian::RuleSet& rules)
    {
        std::printf("conflict stage\n");

        // (a) the necrotic fire of a damage effect marked as unspecified
        auto tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo,
            "OCF_MgefSpellDamage_NecroticFire", "KIT_MagicElement_Necrotic" }) })), rules);
        Check(!Has(tags, "fire") && Has(tags, "necrotic") && Dropped(tags, "fire"),
            "unspecified mark: framework fire dropped, necrotic kept");
        Check(tags.elementSources.size() == 1 && SourceOf(tags, "necrotic") == "framework"
            && tags.elementSource == "framework", "unspecified mark: sources follow the tags left");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedDar, "KIT_MagicElement_Fire",
            "KIT_MagicElement_Necrotic" }) })), rules);
        Check(!Has(tags, "fire") && Has(tags, "necrotic"), "the mark's other editor ID works the same");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Frost",
            "KIT_MagicElement_Shock", "KIT_MagicElement_Poison", "KIT_MagicElement_Necrotic" }) })), rules);
        Check(tags.elements == std::set<std::string>{ "necrotic" } && tags.conflictDropped.size() == 3,
            "unspecified mark: frost, shock and poison dropped too");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "MagicDamageFrost", "KIT_MagicElement_Fire" },
            "FrostResist") })), rules);
        Check(!Has(tags, "fire") && Has(tags, "frost") && Dropped(tags, "fire"),
            "record frost on the same effect: framework fire dropped");
        Check(SourceOf(tags, "frost") == "mgef" && tags.elementSource == "mgef", "record frost keeps its mgef source");

        json resistBuff = Effect({ "KIT_MagicElement_Frost" }, "None", "PeakValueModifier");
        resistBuff["primaryAV"] = "FireResist";
        resistBuff["hostile"] = false;
        tags = Librarian::Classify(Spell(json::array({ resistBuff })), rules);
        Check(!Has(tags, "frost") && Has(tags, "fire") && SourceOf(tags, "fire") == "manual",
            "an effect-level manual rule is record evidence: its fire takes the framework frost");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "UserFire" }) })), rules);
        Check(!Has(tags, "fire"), "a user's own tier ranks below the record and is contradicted like framework");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire",
            "KIT_TechCurse" }) })), rules);
        Check(!Has(tags, "fire") && tags.techniques.contains("curse") && tags.techniqueSource == "framework",
            "techniques are untouched by the stage");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire",
            "MagicDamageResist" }) })), rules);
        Check(tags.elements == std::set<std::string>{ "arcane" } && Dropped(tags, "fire")
            && tags.elementSource == "mgef", "emptied by the stage: MagicDamageResist gives arcane");

        tags = Librarian::Classify(Spell(json::array({ HiddenHelper({ kUnspecifiedApo, "KIT_MagicElement_Fire" }) })), rules);
        Check(!Has(tags, "fire") && Dropped(tags, "fire"),
            "all effects hidden: they all count, the mark on the hidden effect too");
    }

    void ConflictApplies(const Librarian::RuleSet& rules)
    {
        std::printf("conflict stage through applies\n");

        // Desecration's flame cloak: fire on the body, the mark on its damage
        auto tags = Librarian::Classify(Spell(json::array({ Cloak({ "OCF_MgefSpellDamage_NecroticFireCloak" },
            json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Necrotic" }) })) })), rules);
        Check(!Has(tags, "fire") && Has(tags, "necrotic"), "cloak body fire, mark on its applied damage: dropped");

        tags = Librarian::Classify(Spell(json::array({ Cloak({ "KIT_MagicElement_Fire" },
            json::array({ Effect({ "MagicDamageFrost" }, "FrostResist") })) })), rules);
        Check(!Has(tags, "fire") && Has(tags, "frost"), "cloak body fire, record frost on its applied damage: dropped");

        tags = Librarian::Classify(Spell(json::array({ Cloak({ "KIT_MagicElement_Fire" },
            json::array({ Effect({ kUnspecifiedApo }), Effect({ "KIT_MagicElement_Necrotic" }) })) })), rules);
        Check(Has(tags, "fire"), "cloak body fire, only one of two applied damages marked: kept");

        json heal = Effect({}, "None");
        heal["detrimental"] = false;
        heal["hostile"] = false;
        tags = Librarian::Classify(Spell(json::array({ Cloak({ "KIT_MagicElement_Fire" },
            json::array({ Effect({ kUnspecifiedApo }), heal })) })), rules);
        Check(!Has(tags, "fire") && Dropped(tags, "fire"),
            "cloak body fire, marked damage plus a heal it also applies: the heal is no damage, dropped");

        tags = Librarian::Classify(Spell(json::array({ Cloak({},
            json::array({ Effect({ "KIT_MagicElement_Fire" }), Effect({ "MagicDamageFrost" }, "FrostResist") })) })), rules);
        Check(Has(tags, "fire") && Has(tags, "frost"), "sibling applied effects: frost on one keeps fire on the other");

        tags = Librarian::Classify(Spell(json::array({ Cloak({},
            json::array({ Effect({ "KIT_MagicElement_Fire" }), Effect({ kUnspecifiedApo }) })) })), rules);
        Check(Has(tags, "fire") && tags.conflictDropped.empty(), "sibling applied effects: a mark on one keeps fire on the other");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "KIT_MagicElement_Fire" }),
            Effect({ "MagicDamageFrost" }, "FrostResist") })), rules);
        Check(Has(tags, "fire") && Has(tags, "frost"), "the same two effects at the top level: both kept");
    }

    void ConflictKeeps(const Librarian::RuleSet& rules)
    {
        std::printf("conflict stage keeps\n");

        auto tags = Librarian::Classify(Spell(json::array({ Effect({ "MagicDamageFrost" }, "FrostResist"),
            Effect({ "KIT_MagicElement_Fire" }, "FireResist") })), rules);
        Check(Has(tags, "fire") && Has(tags, "frost") && tags.conflictDropped.empty(),
            "frost on one effect, fire (with its resistance) on another: both kept");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire" }),
            Effect({ "KIT_MagicElement_Fire" }) })), rules);
        Check(Has(tags, "fire") && SourceOf(tags, "fire") == "framework",
            "one framework rule on a contradicted and a clean effect: kept");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire" }),
            Effect({ "MagicDamageFire" }, "FireResist") })), rules);
        Check(Has(tags, "fire") && SourceOf(tags, "fire") == "mgef", "record fire elsewhere in the spell keeps it");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire" }) }),
            kManualSpell), rules);
        Check(Has(tags, "fire") && SourceOf(tags, "fire") == "manual", "a manual fire keeps it, sourced manual");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo }) }), "Test.esp|0x000802",
            { "KIT_SpellFire" }), rules);
        Check(Has(tags, "fire"), "spell-level framework fire is not tied to an effect: kept");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "ADAR_SPEL_Water_Bolt" }),
            Effect({ kUnspecifiedApo, "ADAR_SPEL_Water_Spray" }) })), rules);
        Check(Has(tags, "water") && tags.conflictDropped.empty(),
            "water by ResistMagic, even beside the mark: not an engine element, kept");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "MagicDamageFire", "KIT_MagicElement_AshFire" },
            "FireResist") })), rules);
        Check(Has(tags, "fire") && Has(tags, "earth"), "ash: record fire does not take the framework earth");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "KIT_MagicElement_Fire" }),
            HiddenHelper({ kUnspecifiedApo }) })), rules);
        Check(Has(tags, "fire"), "the mark on a hidden helper is no evidence: kept");

        tags = Librarian::Classify(Spell(json::array({ Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire" }) })),
            MakeRules(false));
        Check(Has(tags, "fire"), "no marks loaded: only the same-effect test runs, fire kept");
    }

    void Sources(const Librarian::RuleSet& rules)
    {
        std::printf("sources and removals\n");

        auto tags = Librarian::Classify(Spell(json::array({ Effect({ "KIT_MagicElement_Fire" }) }), kManualSpell), rules);
        Check(tags.elementSource == "manual" && SourceOf(tags, "fire") == "manual",
            "framework met first, manual too: the strongest tier, not the first");

        json raise = Effect({ "MagicDamageFire", "KIT_MagicElement_Necrotic" }, "FireResist", "Reanimate");
        tags = Librarian::Classify(Spell(json::array({ raise })), rules);
        Check(!Has(tags, "fire") && tags.elementSource == "framework" && SourceOf(tags, "fire").empty(),
            "a removal takes the tag's source with it, the axis source follows");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "AlphaAir", "ZetaWater" }) })), rules);
        Check(tags.elementSource == "zeta" && SourceOf(tags, "air") == "alpha",
            "equal ranks: the earlier rule's tier, not the first tag by name");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "MagicDamageResist", "TestNoArcane" }) })), rules);
        Check(tags.elements.empty(), "a tag a rule removed stays off through the noElement pass");

        tags = Librarian::Classify(Spell(json::array({ Effect({ "KIT_TechCurse" }, "MagicResist", "Cloak") })), rules);
        Check(tags.techniques == std::set<std::string>{ "cloak", "curse" } && tags.techniqueSource == "mgef",
            "mixed technique axis: the strongest tier");

        const json dump = { { "spells", json::array({
            Spell(json::array({ Effect({ kUnspecifiedApo, "OCF_MgefSpellDamage_NecroticFire" }) }), "A.esp|0x000001"),
            Spell(json::array({ Effect({ "MagicDamageFire" }, "FireResist") }), "A.esp|0x000002"),
            Spell(json::array({ Effect({ "MagicDamageFire", "KIT_TechCurse" }, "FireResist") }), "A.esp|0x000003") }) } };
        Librarian::CatalogStats stats;
        const json catalog = Librarian::BuildCatalog(dump, rules, stats);
        const json& first = catalog["spells"]["A.esp|0x000001"];
        Check(first["elementSources"] == json::object({ { "necrotic", "framework" } })
            && first["elements"] == json::array({ "necrotic" }), "catalog: elementSources written per element");
        Check(catalog["spells"]["A.esp|0x000002"]["elementSources"] == json::object({ { "fire", "mgef" } })
            && catalog["version"] == Librarian::CATALOG_VERSION, "catalog: version unchanged, record fire is mgef");
        Check(catalog["spells"]["A.esp|0x000003"]["confidence"] == Librarian::CONFIDENCE_FRAMEWORK,
            "catalog: record element, framework technique - confidence follows the weaker axis");
        Check(stats.conflictSpells == 1 && stats.conflictDropped.size() == 1 && stats.conflictDropped.at("fire") == 1,
            "catalog: the stage's drops are counted");
    }

    void BadInput(const Librarian::RuleSet& rules)
    {
        std::printf("bad input\n");

        Check(Librarian::Classify(json::array(), rules).Empty(), "a spell that is not an object: no tags");
        Check(Librarian::Classify(json{ { "persistentId", "X.esp|0x1" } }, rules).Empty(), "no effects: no tags");
        Check(Librarian::Classify(json{ { "effects", "nope" } }, rules).Empty(), "effects not an array: no tags");
        json odd = Spell(json::array({ 5, Effect({ kUnspecifiedApo, "KIT_MagicElement_Fire" }) }));
        auto tags = Librarian::Classify(odd, rules);
        Check(!Has(tags, "fire"), "a non-object effect before the real one is skipped, ids still line up");
        Check(rules.unspecifiedDamageKeywords.size() == 2, "marks: non-strings and empty names ignored");

        Librarian::RuleSet marksOnly;
        std::string logged;
        {
            LogCapture capture;
            Librarian::AppendRules(json::parse(R"({ "unspecifiedDamageKeywords": ["X_Mark"] })"), "20_x.json", marksOnly);
            logged = capture.Text();
        }
        Check(marksOnly.unspecifiedDamageKeywords.size() == 1 && logged.empty(),
            "a file of marks alone (no rules array) loads without a warning");
    }
}

int RunLibrarianSelfTest()
{
    const Librarian::RuleSet rules = MakeRules();
    ConflictStage(rules);
    ConflictApplies(rules);
    ConflictKeeps(rules);
    Sources(rules);
    BadInput(rules);
    RunAdapterSelfTest(g_passed, g_failed);
    RunEnvironmentSelfTest(g_passed, g_failed);
    RunScanRecordSelfTest(g_passed, g_failed);
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
