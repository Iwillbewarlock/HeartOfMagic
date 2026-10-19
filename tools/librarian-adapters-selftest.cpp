// =============================================================================
// librarian-test --self-test, adapter part  -  the planner on made-up spells
// =============================================================================
//
// What a dump cannot pin down one case at a time: tierMin on the catalog's
// per-element tiers (and a catalog without them), the requires.config gate,
// and the per-effect damage lines - which effects deal an element's damage,
// the leak check against every item using the effect, effects an earlier line
// already wrote. Needs no dump, no adapter directory.

#include "Common.h"
#include "LibrarianSelfTest.h"

#include "librarian/PerkAdapters.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
    using json = nlohmann::json;
    namespace Adapters = Librarian::Adapters;

    int* g_passed = nullptr;
    int* g_failed = nullptr;

    void Check(bool ok, const char* what)
    {
        (ok ? *g_passed : *g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }

    // A damage-shaped effect entry. `form` is its key.
    json Hit(const char* form, const char* resistance, const char* archetype = "ValueModifier",
        const char* av = "Health", double magnitude = 10, bool hidden = false, std::vector<std::string> keywords = {})
    {
        return { { "form", form }, { "archetype", archetype }, { "resistance", resistance }, { "primaryAV", av },
            { "detrimental", true }, { "magnitude", magnitude }, { "conditions", 0 },
            { "flags", { { "hideInUI", hidden } } }, { "keywords", keywords } };
    }

    json Entry(std::map<std::string, std::string> elementTiers, bool withSources = true)
    {
        json entry = { { "elements", json::array() }, { "techniques", json::array() } };
        json sources = json::object();
        for (const auto& [element, tier] : elementTiers) {
            entry["elements"].push_back(element);
            sources[element] = tier;
        }
        if (withSources) entry["elementSources"] = sources;
        return entry;
    }

    // A made-up load order: spells (some in the catalog) and other items
    // (scrolls, enchantments) by the effects they use.
    struct World
    {
        std::vector<json> spells;                         // scan-shaped, with persistentId
        json catalog = json::object();                    // id -> entry
        std::map<std::string, std::vector<std::string>> items;   // effect key -> non-spell item ids

        void Add(const char* id, json effects, const json* entry, std::vector<std::string> keywords = {})
        {
            spells.push_back({ { "persistentId", id }, { "name", id }, { "effects", std::move(effects) }, { "keywords", keywords } });
            if (entry) catalog[id] = *entry;
        }

        Adapters::Plan Plan(const std::vector<Adapters::File>& files) const
        {
            Adapters::PlanSource source;
            source.forEachCatalogSpell = [this](const auto& visit) {
                for (const auto& spell : spells) {
                    const std::string id = spell["persistentId"];
                    if (catalog.contains(id)) visit(id, spell, catalog[id]);
                }
            };
            source.usersOf = [this](const std::string& key) {
                std::vector<Adapters::EffectUser> users;
                for (const auto& spell : spells) {
                    const bool uses = std::ranges::any_of(spell["effects"], [&](const json& e) { return Adapters::EffectKey(e) == key; });
                    if (!uses) continue;
                    const std::string id = spell["persistentId"];
                    users.push_back({ id, &spell, catalog.contains(id) ? &catalog[id] : nullptr });
                }
                if (const auto found = items.find(key); found != items.end()) {
                    for (const auto& item : found->second) users.push_back({ item, nullptr, nullptr });
                }
                return users;
            };
            return Adapters::BuildPlan(files, source);
        }
    };

    Adapters::File ParseOne(const char* text, std::vector<std::string>& problems)
    {
        Adapters::File file;
        Adapters::ParseFile(json::parse(text), "test.json", file, problems);
        return file;
    }

    Adapters::File ParseOne(const char* text)
    {
        std::vector<std::string> problems;
        return ParseOne(text, problems);
    }

    bool Wrote(const Adapters::Plan& plan, const char* keyword, const char* effect)
    {
        return std::ranges::any_of(plan.writes, [&](const Adapters::Write& w) { return w.keyword == keyword && w.effectKey == effect; });
    }

    constexpr const char* kFireLine = R"({ "requires": { "always": true }, "adapters": [
        { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"] }, "tierMin": "manual" } ] })";

    void DamageEffects()
    {
        std::printf("damage lines: which effects\n");
        World world;
        const json fire = Entry({ { "fire", "mgef" } });
        world.Add("A|0x1", json::array({ Hit("E:vm", "FireResist"), Hit("E:hidden", "FireResist", "ValueModifier", "Health", 5, true),
            Hit("E:script", "FireResist", "Script", "None"), Hit("E:scriptMagic", "MagicResist", "Script", "None"),
            Hit("E:absorb", "FireResist", "Absorb"), Hit("E:stamina", "FireResist", "ValueModifier", "Stamina"),
            Hit("E:zero", "FireResist", "ValueModifier", "Health", 0), Hit("E:plain", "MagicResist") }), &fire);
        json helper = Hit("E:helper", "FireResist");
        helper["detrimental"] = false;
        auto& effects = world.spells.back()["effects"];
        effects.push_back(helper);
        effects.push_back(Hit("E:hidScript", "FireResist", "Script", "None", 99, true));
        effects.push_back(Hit("E:hidPlain", "None", "ValueModifier", "Health", 5, true));
        effects.push_back(Hit("E:armor", "DamageResist"));
        effects.push_back(Hit("E:marked", "FireResist", "ValueModifier", "Health", 10, false, { "APO_MagicDamageUnspecified" }));

        const auto plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"], "keywordsNone": ["APO_MagicDamageUnspecified"] },
              "tierMin": "manual" } ] })") });
        Check(Wrote(plan, "MagicDamageFire", "E:vm"), "normal: a fire spell's Health damage effect gets MagicDamageFire");
        Check(Wrote(plan, "MagicDamageFire", "E:hidden"), "a hidden effect that deals real fire damage is written too");
        Check(Wrote(plan, "MagicDamageFire", "E:script"), "a Script effect resisted by FireResist counts as fire damage");
        Check(!Wrote(plan, "MagicDamageFire", "E:scriptMagic"), "a Script effect with another resistance does not");
        Check(!Wrote(plan, "MagicDamageFire", "E:absorb"), "Absorb is not damage of an element");
        Check(!Wrote(plan, "MagicDamageFire", "E:stamina"), "a value modifier off Health is not written");
        Check(!Wrote(plan, "MagicDamageFire", "E:zero") && !Wrote(plan, "MagicDamageFire", "E:helper"),
            "magnitude 0 and a non-detrimental effect are not written");
        Check(!Wrote(plan, "MagicDamageFire", "E:plain"), "edge: a Health damage effect resisted by MagicResist is not fire, even in a fire-only spell");
        Check(!Wrote(plan, "MagicDamageFire", "E:hidScript"), "a hidden Script effect resisted by FireResist (a perk's helper) is not");
        Check(!Wrote(plan, "MagicDamageFire", "E:hidPlain"), "a hidden damage effect with no element resistance is not");
        Check(!Wrote(plan, "MagicDamageFire", "E:armor"), "an effect resisted by armor (DamageResist) is not");
        Check(!Wrote(plan, "MagicDamageFire", "E:marked"), "an effect carrying one of damage.keywordsNone is not");
        Check(plan.stats[0][0].writes == 3 && plan.stats[0][0].gain == 1, "one spell gains, three effects written");
    }

    void DamageSpells()
    {
        std::printf("damage lines: which spells\n");
        World world;
        const json mgef = Entry({ { "fire", "mgef" } });
        const json manual = Entry({ { "fire", "manual" } });
        const json framework = Entry({ { "fire", "framework" } });
        const json necrotic = Entry({ { "necrotic", "framework" } });
        const json mixed = Entry({ { "fire", "mgef" }, { "frost", "mgef" } });
        const json old = Entry({ { "fire", "mgef" } }, false);
        world.Add("Mgef|0x1", json::array({ Hit("E:1", "FireResist") }), &mgef);
        world.Add("Manual|0x1", json::array({ Hit("E:2", "FireResist") }), &manual);
        world.Add("Framework|0x1", json::array({ Hit("E:3", "FireResist") }), &framework);
        world.Add("Necrotic|0x1", json::array({ Hit("E:4", "MagicResist") }), &necrotic);
        world.Add("Mixed|0x1", json::array({ Hit("E:5", "FireResist"), Hit("E:6", "FrostResist"), Hit("E:7", "MagicResist") }), &mixed);
        world.Add("Old|0x1", json::array({ Hit("E:8", "FireResist") }), &old);
        world.Add("Agree|0x1", json::array({ Hit("E:9", "FireResist", "ValueModifier", "Health", 10, false, { "MagicDamageFire" }) }), &mgef);

        auto plan = world.Plan({ ParseOne(kFireLine) });
        Check(Wrote(plan, "MagicDamageFire", "E:1") && Wrote(plan, "MagicDamageFire", "E:2"),
            "tierMin manual takes fire from the record and from a manual rule");
        Check(!Wrote(plan, "MagicDamageFire", "E:3"), "fail: fire from a framework keyword only is skipped");
        Check(!Wrote(plan, "MagicDamageFire", "E:4"), "a necrotic spell (no fire in the catalog) gets nothing");
        Check(Wrote(plan, "MagicDamageFire", "E:5") && !Wrote(plan, "MagicDamageFire", "E:6") && !Wrote(plan, "MagicDamageFire", "E:7"),
            "fire + frost spell: only the FireResist effect, not the frost one nor the neutral one");
        Check(!Wrote(plan, "MagicDamageFire", "E:8") && plan.entriesWithoutSources > 0,
            "a catalog entry without elementSources is framework for tierMin, and counted");
        Check(!Wrote(plan, "MagicDamageFire", "E:9") && plan.stats[0][0].agree == 1, "an effect that has the keyword: agree, no write");

        plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"] }, "tierMin": "mgef" } ] })") });
        Check(Wrote(plan, "MagicDamageFire", "E:1") && !Wrote(plan, "MagicDamageFire", "E:2"), "tierMin mgef leaves the manual fire out");

        plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"] } } ] })") });
        Check(Wrote(plan, "MagicDamageFire", "E:3") && Wrote(plan, "MagicDamageFire", "E:8"), "without tierMin any tier counts");

        World douse;
        const json water = Entry({ { "water", "manual" } });
        const json frost = Entry({ { "frost", "mgef" } });
        const json fire = Entry({ { "fire", "mgef" } });
        douse.Add("Water|0x1", json::array({ Hit("W:1", "MagicResist") }), &water);
        douse.Add("Frost|0x1", json::array({ Hit("W:2", "FrostResist") }), &frost);
        douse.Add("Fire|0x1", json::array({ Hit("W:3", "FireResist"), Hit("W:4", "MagicResist") }), &fire);
        plan = douse.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "waterbearingdt", "damage": { "elements": ["frost", "water", "air"] }, "tierMin": "manual" } ] })") });
        Check(Wrote(plan, "waterbearingdt", "W:1") && Wrote(plan, "waterbearingdt", "W:2") && plan.writes.size() == 2,
            "a line of several elements: water damage (neutral resistance), frost damage, nothing of the fire spell");
        World hiddenWater;
        hiddenWater.Add("Water|0x1", json::array({ Hit("H:1", "MagicResist", "ValueModifier", "Health", 5, true) }), &water);
        plan = hiddenWater.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "waterbearingdt", "damage": { "elements": ["water"] } } ] })") });
        Check(plan.writes.empty(), "a hidden neutral effect does not stand for water");
    }

    void DamageLeaks()
    {
        std::printf("damage lines: shared effects\n");
        World world;
        const json fire = Entry({ { "fire", "mgef" } });
        const json frost = Entry({ { "frost", "mgef" } });
        world.Add("A|0x1", json::array({ Hit("S:both", "FireResist"), Hit("S:scroll", "FireResist"),
            Hit("S:frost", "FireResist"), Hit("S:outside", "FireResist") }), &fire);
        world.Add("B|0x1", json::array({ Hit("S:both", "FireResist") }), &fire);
        world.Add("C|0x1", json::array({ Hit("S:frost", "FireResist"), Hit("X:1", "FrostResist") }), &frost);
        world.Add("Outside|0x1", json::array({ Hit("S:outside", "FireResist") }), nullptr);
        world.items["S:scroll"] = { "ScrollOfFire|0x1" };

        auto plan = world.Plan({ ParseOne(kFireLine) });
        const auto& stats = plan.stats[0][0];
        Check(Wrote(plan, "MagicDamageFire", "S:both"), "edge: an effect two fire spells share is written once");
        Check(!Wrote(plan, "MagicDamageFire", "S:scroll") && stats.blockedByItem == 1, "fail: a scroll using the effect blocks it");
        Check(std::ranges::any_of(stats.leakExamples, [](const std::string& e) { return e.find("ScrollOfFire|0x1") != std::string::npos; }),
            "the leak example names the blocking item");
        Check(!Wrote(plan, "MagicDamageFire", "S:frost"), "an effect a frost spell also uses is blocked");
        Check(!Wrote(plan, "MagicDamageFire", "S:outside"), "an effect a spell outside the catalog uses is blocked");
        Check(stats.leaks == 3 && stats.gain == 2, "three leaks, two spells gain");

        plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"] }, "tierMin": "manual", "sharedEffects": "allow" } ] })") });
        Check(Wrote(plan, "MagicDamageFire", "S:scroll") && plan.stats[0][0].leaks == 0, "sharedEffects allow skips the leak check");

        World conditioned;
        json bonus = Hit("P:1", "FireResist");
        bonus["conditions"] = 1;
        conditioned.Add("A|0x1", json::array({ bonus }), &fire);
        plan = conditioned.Plan({ ParseOne(kFireLine) });
        Check(plan.writes.empty() && plan.stats[0][0].conditioned == 1, "an effect only used with conditions (a perk bonus) is skipped");

        // Another fire spell uses the same effect with conditions, or at magnitude 0
        conditioned.Add("B|0x1", json::array({ Hit("P:1", "FireResist") }), &fire);
        conditioned.Add("C|0x1", json::array({ Hit("P:1", "FireResist", "ValueModifier", "Health", 0) }), &fire);
        plan = conditioned.Plan({ ParseOne(kFireLine) });
        Check(Wrote(plan, "MagicDamageFire", "P:1") && plan.stats[0][0].gain == 3,
            "edge: a fire spell using the effect with conditions or at magnitude 0 does not block it, and gains");

        // An ordinary keyword line earlier in the plan writes the same effect
        plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "MagicDamageFire", "tags": { "all": ["fire"] }, "effect": { "resistance": "FireResist" } },
            { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"] }, "tierMin": "manual" } ] })") });
        const bool firstWrote = std::ranges::any_of(plan.writes, [](const Adapters::Write& w) {
            return w.line == 0 && w.effectKey == "S:both"; });
        const bool secondWrote = std::ranges::any_of(plan.writes, [](const Adapters::Write& w) {
            return w.line == 1 && w.effectKey == "S:both"; });
        Check(firstWrote && !secondWrote, "an effect an earlier line wrote is not planned again");

        // An earlier line's write of an excluding mark counts like the plugin's own
        plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "APO_MagicDamageUnspecified", "tags": { "all": ["fire"] }, "effect": { "resistance": "FireResist" } },
            { "keyword": "MagicDamageFire", "damage": { "elements": ["fire"], "keywordsNone": ["APO_MagicDamageUnspecified"] } } ] })") });
        Check(std::ranges::none_of(plan.writes, [](const Adapters::Write& w) { return w.line == 1 && w.effectKey == "S:both"; }),
            "a mark an earlier line wrote keeps damage.keywordsNone's effect out");
    }

    void TierAndConfig()
    {
        std::printf("tierMin on ordinary lines, requires.config, parsing\n");
        World world;
        const json mgef = Entry({ { "fire", "mgef" } });
        const json framework = Entry({ { "fire", "framework" } });
        world.Add("A|0x1", json::array({ Hit("T:1", "FireResist", "Cloak") }), &mgef);
        world.Add("B|0x1", json::array({ Hit("T:2", "FireResist", "Cloak") }), &framework);
        const auto plan = world.Plan({ ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "Frostwalker_FireCloak_Keyword", "tags": { "all": ["fire"] }, "tierMin": "manual",
              "effect": { "archetype": "Cloak" } } ] })") });
        Check(Wrote(plan, "Frostwalker_FireCloak_Keyword", "T:1") && !Wrote(plan, "Frostwalker_FireCloak_Keyword", "T:2"),
            "tierMin on an ordinary line: record fire yes, framework fire no");

        const json on = json::parse(R"({ "environment": { "enabled": true }, "other": 3, "flat": false })");
        const json off = json::parse(R"({ "environment": { "enabled": false } })");
        Check(!Adapters::ConfigFlagOn(off, "environment.enabled"), "config: false switches the gate off");
        Check(Adapters::ConfigFlagOn(on, "environment.enabled") && Adapters::ConfigFlagOn(json::object(), "environment.enabled"),
            "config: true, and a missing key, keep it on");
        Check(Adapters::ConfigFlagOn(on, "other.enabled") && Adapters::ConfigFlagOn(json::array(), "environment.enabled"),
            "config: a part that is not an object keeps it on");
        Check(!Adapters::ConfigFlagOn(on, "flat"), "config: a one-part path reads the top level");

        Adapters::File gated = ParseOne(R"({ "requires": { "always": true, "config": "environment.enabled" }, "adapters": [] })");
        const auto none = [](const std::string&) { return false; };
        const auto all = [](const std::string&) { return true; };
        Check(gated.requiresConfig == "environment.enabled", "requires.config is read");
        Check(!Adapters::FileApplies(gated, off, all) && Adapters::FileApplies(gated, on, none),
            "a gated file: off with the switch off, on with it on (always)");
        Adapters::File packs = ParseOne(R"({ "requires": { "any": ["Pack.esp"], "config": "environment.enabled" }, "adapters": [] })");
        Check(!Adapters::FileApplies(packs, on, none) && Adapters::FileApplies(packs, on, all) && !Adapters::FileApplies(packs, off, all),
            "config and any: both must hold");

        Adapters::File typo = ParseOne(R"({ "requires": { "always": true, "confg": "environment.enabled" }, "adapters": [] })");
        Adapters::File wrongType = ParseOne(R"({ "requires": { "always": true, "config": false }, "adapters": [] })");
        Check(!Adapters::FileApplies(typo, on, all) && !Adapters::FileApplies(wrongType, on, all),
            "fail: a requires it cannot read (a mistyped or wrong-typed gate) keeps the file off");

        std::vector<std::string> problems;
        Adapters::File bad = ParseOne(R"({ "requires": { "always": true }, "adapters": [
            { "keyword": "K1", "tierMin": "record" },
            { "formList": "X.esp|0x1", "damage": { "elements": ["fire"] } },
            { "keyword": "K3", "damage": { "elements": [] } },
            { "keyword": "K4", "damage": { "elements": ["fire"], "radius": 3 } },
            { "keyword": "K5", "damage": { "elements": ["fire"] }, "tierMin": "framework" } ] })", problems);
        Check(bad.lines.size() == 5 && !bad.lines[0].enabled && !bad.lines[1].enabled && !bad.lines[2].enabled && !bad.lines[3].enabled,
            "fail: an unknown tierMin, damage on a formList line, no elements, an unknown damage key - each line off");
        Check(bad.lines[4].enabled && bad.lines[4].tierMinRank == 1 && problems.size() == 4, "a good line beside them stays on");
    }
}

void RunAdapterSelfTest(int& passed, int& failed)
{
    g_passed = &passed;
    g_failed = &failed;
    DamageEffects();
    DamageSpells();
    DamageLeaks();
    TierAndConfig();
}
