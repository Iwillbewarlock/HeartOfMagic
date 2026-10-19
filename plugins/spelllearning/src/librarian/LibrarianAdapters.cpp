#include "librarian/PerkAdapters.h"
#include "librarian/AdapterMatch.h"
#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/VanillaCastingPerks.h"

#include <algorithm>
#include <cctype>
#include <string_view>

// =============================================================================
// LibrarianAdapters - parsing adapter files and planning keyword writes
//
// Pure: no game types, so tools/librarian-test runs the same plan the game
// does. The semantics are in PerkAdapters.h.
// =============================================================================

namespace Librarian::Adapters
{
    using namespace Match;

    namespace
    {
        constexpr const char* VANILLA_FILE = "vanilla.json";

        // ---------------------------------------------------------------------
        // Parsing
        // ---------------------------------------------------------------------

        // Reads one condition object. Every key it understands is consumed;
        // any other key is reported and makes the result unusable.
        struct Reader
        {
            const json& object;
            std::vector<std::string>& problems;
            std::string where;
            bool ok = true;

            template <class T>
            void Optional(const char* key, std::optional<T>& target, bool (json::*isType)() const noexcept)
            {
                const auto found = object.find(key);
                if (found == object.end()) return;
                if (!((*found).*isType)()) {
                    problems.push_back(where + ": '" + key + "' has the wrong type");
                    ok = false;
                    return;
                }
                target = found->get<T>();
            }

            void List(const char* key, std::vector<std::string>& target)
            {
                const auto found = object.find(key);
                if (found == object.end()) return;
                const auto take = [&](const json& value) {
                    if (value.is_string()) {
                        target.push_back(value.get<std::string>());
                    } else {
                        problems.push_back(where + ": '" + key + "' holds a non-string");
                        ok = false;
                    }
                };
                if (found->is_array()) {
                    for (const auto& value : *found) take(value);
                } else {
                    take(*found);
                }
            }

            void RejectUnknown(std::initializer_list<std::string_view> known)
            {
                for (const auto& [key, value] : object.items()) {
                    if (std::ranges::find(known, std::string_view(key)) == known.end()) {
                        problems.push_back(where + ": unknown key '" + key + "'");
                        ok = false;
                    }
                }
            }
        };

        bool ParseEffect(const json& object, const std::string& where, EffectCondition& effect,
            std::vector<std::string>& problems)
        {
            if (!object.is_object()) {
                problems.push_back(where + ": not an object");
                return false;
            }
            Reader read{ object, problems, where };
            read.Optional("archetype", effect.archetype, &json::is_string);
            read.List("archetypeAny", effect.archetypeAny);
            read.List("archetypeNone", effect.archetypeNone);
            read.Optional("primaryAV", effect.primaryAV, &json::is_string);
            read.List("primaryAVAny", effect.primaryAVAny);
            read.Optional("resistance", effect.resistance, &json::is_string);
            read.Optional("detrimental", effect.detrimental, &json::is_boolean);
            read.Optional("delivery", effect.delivery, &json::is_string);
            read.Optional("minDuration", effect.minDuration, &json::is_number);
            read.Optional("noDuration", effect.noDuration, &json::is_boolean);
            read.List("hazardSource", effect.hazardSource);
            read.Optional("explodes", effect.explodes, &json::is_boolean);
            read.Optional("visible", effect.visible, &json::is_boolean);
            read.List("summonedAny", effect.summonedAny);
            read.List("summonedNone", effect.summonedNone);
            read.RejectUnknown({ "archetype", "archetypeAny", "archetypeNone", "primaryAV", "primaryAVAny",
                "resistance", "detrimental", "delivery", "minDuration", "noDuration", "hazardSource",
                "explodes", "visible", "summonedAny", "summonedNone" });
            return read.ok;
        }

        bool ParseEffectList(const json& line, const char* key, const std::string& where,
            std::vector<EffectCondition>& target, std::vector<std::string>& problems)
        {
            const auto found = line.find(key);
            if (found == line.end()) return true;
            if (!found->is_array()) {
                problems.push_back(where + ": '" + key + "' is not a list");
                return false;
            }
            bool ok = true;
            for (const auto& entry : *found) {
                EffectCondition condition;
                ok = ParseEffect(entry, where + "." + key, condition, problems) && ok;
                target.push_back(std::move(condition));
            }
            return ok;
        }

        bool ParseLine(const json& object, const std::string& where, Line& line,
            std::vector<std::string>& problems)
        {
            if (!object.is_object()) {
                problems.push_back(where + ": not an object");
                return false;
            }
            Reader read{ object, problems, where };
            std::optional<std::string> keyword;
            std::optional<std::string> formList;
            std::optional<std::string> castingPerk;
            read.Optional("keyword", keyword, &json::is_string);
            read.Optional("formList", formList, &json::is_string);
            read.Optional("castingPerk", castingPerk, &json::is_string);
            line.keyword = keyword.value_or("");
            line.formList = formList.value_or("");
            line.castingPerk = castingPerk.value_or("");
            if (!line.keyword.empty() + !line.formList.empty() + !line.castingPerk.empty() != 1) {
                problems.push_back(where + ": needs exactly one of \"keyword\", \"formList\" and \"castingPerk\"");
                return false;
            }
            if (!line.castingPerk.empty() && line.castingPerk != "vanilla") {
                problems.push_back(where + ": \"castingPerk\" knows only \"vanilla\"");
                return false;
            }
            std::optional<std::string> keywordForm;
            read.Optional("keywordForm", keywordForm, &json::is_string);
            line.keywordForm = keywordForm.value_or("");
            if (!line.keywordForm.empty() && line.keyword.empty()) {
                problems.push_back(where + ": \"keywordForm\" goes with \"keyword\"");
                return false;
            }

            std::optional<bool> enabled;
            read.Optional("enabled", enabled, &json::is_boolean);
            line.enabled = enabled.value_or(true);
            std::optional<std::string> sharedEffects;
            read.Optional("sharedEffects", sharedEffects, &json::is_string);
            line.allowSharedEffects = sharedEffects == "allow";
            if (sharedEffects && (!line.allowSharedEffects || line.keyword.empty())) {
                problems.push_back(where + ": \"sharedEffects\" is \"allow\", on a \"keyword\" line only");
                return false;
            }
            read.List("keywordsNone", line.keywordsNone);
            read.List("formListsNone", line.formListsNone);

            std::optional<std::string> tierMin;
            read.Optional("tierMin", tierMin, &json::is_string);
            if (tierMin) {
                if (*tierMin != SOURCE_MGEF && *tierMin != SOURCE_MANUAL && *tierMin != SOURCE_FRAMEWORK) {
                    problems.push_back(where + ": \"tierMin\" is \"mgef\", \"manual\" or \"framework\"");
                    return false;
                }
                line.tierMin = *tierMin;
                line.tierMinRank = TierRank(*tierMin);
            }

            bool ok = true;
            if (const auto damage = object.find("damage"); damage != object.end()) {
                if (!damage->is_object() || line.keyword.empty()) {
                    problems.push_back(where + ": \"damage\" is an object, on a \"keyword\" line only");
                    return false;
                }
                Reader damageRead{ *damage, problems, where + ".damage" };
                damageRead.List("elements", line.damageElements);
                damageRead.List("keywordsNone", line.damageKeywordsNone);
                damageRead.RejectUnknown({ "elements", "keywordsNone" });
                if (line.damageElements.empty()) {
                    problems.push_back(where + ": \"damage\" needs \"elements\"");
                    damageRead.ok = false;
                }
                ok = damageRead.ok && ok;
            }
            if (const auto tags = object.find("tags"); tags != object.end()) {
                if (tags->is_object()) {
                    Reader tagRead{ *tags, problems, where + ".tags" };
                    tagRead.List("all", line.tags.all);
                    tagRead.List("any", line.tags.any);
                    tagRead.List("none", line.tags.none);
                    tagRead.RejectUnknown({ "all", "any", "none" });
                    ok = tagRead.ok && ok;
                } else {
                    problems.push_back(where + ": 'tags' is not an object");
                    ok = false;
                }
            }
            if (const auto spell = object.find("spell"); spell != object.end()) {
                if (spell->is_object()) {
                    Reader spellRead{ *spell, problems, where + ".spell" };
                    spellRead.Optional("school", line.spell.school, &json::is_string);
                    spellRead.Optional("notSchool", line.spell.notSchool, &json::is_string);
                    spellRead.Optional("tier", line.spell.tier, &json::is_string);
                    spellRead.Optional("casting", line.spell.casting, &json::is_string);
                    spellRead.Optional("twoHanded", line.spell.twoHanded, &json::is_boolean);
                    spellRead.Optional("taughtByTome", line.spell.taughtByTome, &json::is_boolean);
                    spellRead.RejectUnknown({ "school", "notSchool", "tier", "casting", "twoHanded", "taughtByTome" });
                    ok = spellRead.ok && ok;
                } else {
                    problems.push_back(where + ": 'spell' is not an object");
                    ok = false;
                }
            }
            if (const auto effect = object.find("effect"); effect != object.end()) {
                EffectCondition condition;
                ok = ParseEffect(*effect, where + ".effect", condition, problems) && ok;
                line.effect = std::move(condition);
            }
            ok = ParseEffectList(object, "spellHas", where, line.spellHas, problems) && ok;
            ok = ParseEffectList(object, "spellLacks", where, line.spellLacks, problems) && ok;

            read.RejectUnknown({ "keyword", "keywordForm", "formList", "castingPerk", "why", "enabled", "sharedEffects", "tags", "spell",
                "effect", "spellHas", "spellLacks", "keywordsNone", "formListsNone", "tierMin", "damage" });
            return read.ok && ok;
        }

    }

    // =========================================================================
    // FILES
    // =========================================================================

    bool ParseFile(const json& document, const std::string& name, File& file, std::vector<std::string>& problems)
    {
        if (!document.is_object() || !document.contains("adapters") || !document["adapters"].is_array()) {
            problems.push_back(name + ": no \"adapters\" list");
            return false;
        }
        file.name = name;
        file.perkMod = Field(document, "perkMod");

        const auto requirement = document.find("requires");
        if (requirement == document.end() || !requirement->is_object()) {
            problems.push_back(name + ": no \"requires\" object - the file stays off");
        } else {
            Reader read{ *requirement, problems, name + ".requires" };
            std::optional<bool> always;
            read.Optional("always", always, &json::is_boolean);
            file.always = always.value_or(false);
            read.List("any", file.requiresAny);
            std::optional<std::string> config;
            read.Optional("config", config, &json::is_string);
            file.requiresConfig = config.value_or("");
            if (config && config->empty()) {
                problems.push_back(name + ".requires: \"config\" is empty");
                read.ok = false;
            }
            read.RejectUnknown({ "always", "any", "config" });
            // A requires it cannot read in full (a mistyped gate) keeps the file off,
            // never on without its gate
            if (!read.ok) {
                file.always = false;
                file.requiresAny.clear();
            }
        }

        std::size_t index = 0;
        for (const auto& entry : document["adapters"]) {
            Line line;
            const std::string where = name + " line " + std::to_string(index + 1);
            if (!ParseLine(entry, where, line, problems)) {
                line.enabled = false;
            }
            if (!line.keyword.empty() || !line.formList.empty() || !line.castingPerk.empty()) file.lines.push_back(std::move(line));
            ++index;
        }
        return true;
    }

    void SortFiles(std::vector<File>& files)
    {
        std::ranges::sort(files, [](const File& a, const File& b) {
            const bool aVanilla = a.name == VANILLA_FILE;
            const bool bVanilla = b.name == VANILLA_FILE;
            if (aVanilla != bVanilla) return aVanilla;
            return a.name < b.name;
        });
    }

    bool ConfigFlagOn(const json& config, const std::string& dottedPath)
    {
        const json* node = &config;
        std::size_t start = 0;
        while (start <= dottedPath.size()) {
            const std::size_t dot = dottedPath.find('.', start);
            const std::string part = dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!node->is_object()) return true;
            const auto found = node->find(part);
            if (found == node->end()) return true;
            node = &*found;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return !(node->is_boolean() && !node->get<bool>());
    }

    bool FileApplies(const File& file, const json& config, const std::function<bool(const std::string&)>& pluginLoaded)
    {
        if (!file.requiresConfig.empty() && !ConfigFlagOn(config, file.requiresConfig)) return false;
        if (file.always) return true;
        return std::ranges::any_of(file.requiresAny, pluginLoaded);
    }

    // =========================================================================
    // MATCHING
    // =========================================================================

    std::unordered_set<std::string> CarriedKeywords(const json& spell)
    {
        std::unordered_set<std::string> carried;
        const auto addAll = [&](const json& owner) {
            const auto keywords = owner.find("keywords");
            if (keywords == owner.end() || !keywords->is_array()) return;
            for (const auto& keyword : *keywords) {
                if (keyword.is_string()) carried.insert(keyword.get<std::string>());
            }
        };
        addAll(spell);
        for (const auto& effect : EffectsOf(spell)) addAll(effect);
        return carried;
    }

    std::string EffectKey(const json& effect)
    {
        std::string key = Field(effect, "form");
        if (key.empty()) {
            key = Field(effect, "editorId");
            std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        return key;
    }

    std::optional<std::size_t> PickTarget(const json& spell, const json* catalogEntry, const Line& line,
        const std::unordered_set<std::string>& carried, bool* withoutSources)
    {
        bool noSources = false;
        const bool spellMatches = SpellLevelMatch(spell, catalogEntry, line, carried, noSources);
        if (withoutSources) *withoutSources = noSources;
        if (!spellMatches) return std::nullopt;

        const json& effects = EffectsOf(spell);
        static const EffectCondition kAnyEffect;
        const EffectCondition& target = line.effect ? *line.effect : kAnyEffect;
        for (std::size_t i = 0; i < effects.size(); ++i) {
            if (IsVisible(effects[i]) && EffectMatches(effects[i], target)) return i;
        }
        return std::nullopt;
    }

    // =========================================================================
    // PLAN
    // =========================================================================

    Plan BuildPlan(const std::vector<File>& files, const PlanSource& source)
    {
        Plan plan;
        plan.stats.resize(files.size());
        // (effect, keyword) pairs earlier lines of this plan wrote: a damage
        // line reads them with what the plugins wrote
        EffectKeywords effectKeywords;

        // Keywords each spell carries, by persistent id: what the records say
        // (read once) plus what earlier lines gave it.
        std::unordered_map<std::string, std::unordered_set<std::string>> carriedCache;
        const auto carriedBy = [&](const std::string& id, const json& spell) -> const std::unordered_set<std::string>& {
            auto [slot, fresh] = carriedCache.try_emplace(id);
            if (fresh) slot->second = CarriedKeywords(spell);
            return slot->second;
        };

        for (std::size_t f = 0; f < files.size(); ++f) {
            const File& file = files[f];
            plan.stats[f].resize(file.lines.size());

            for (std::size_t l = 0; l < file.lines.size(); ++l) {
                const Line& line = file.lines[l];
                LineStats& stats = plan.stats[f][l];
                const bool isList = !line.formList.empty();
                const bool isCastingPerk = !line.castingPerk.empty();
                const bool defined = isCastingPerk || (isList
                    ? (!source.formListDefined || source.formListDefined(line.formList))
                    : (!source.keywordDefined || source.keywordDefined(line.keyword)));
                const bool live = line.enabled && defined;
                stats.ran = live;
                if (!live && !(source.measureDisabled && defined)) continue;

                if (!line.damageElements.empty()) {
                    PlanDamageLine(f, l, line, live, source, effectKeywords, carriedCache, plan);
                    continue;
                }

                if (isCastingPerk) {
                    source.forEachCatalogSpell([&](const std::string& id, const json& spell, const json& entry) {
                        // The perk sits on the spell record: no target effect to find
                        bool noSources = false;
                        const bool tagsMatch = TagsMatch(entry, line.tags, line.tierMinRank, noSources);
                        if (noSources) ++plan.entriesWithoutSources;
                        if (!tagsMatch || !SpellMatches(spell, line.spell)) return;
                        if (!Field(spell, "castingPerk").empty()) {
                            ++stats.agree;
                            return;
                        }
                        std::string perk = VanillaCastingPerkId(Field(entry, "school"), Field(entry, "tier"));
                        if (perk.empty()) return;
                        ++stats.gain;
                        AddExample(stats.gainExamples, SpellLabel(spell, id));
                        if (live) plan.castingPerkWrites.push_back({ f, l, std::move(perk), id });
                    });
                    continue;
                }

                if (isList) {
                    source.forEachCatalogSpell([&](const std::string& id, const json& spell, const json& entry) {
                        bool noSources = false;
                        const auto target = PickTarget(spell, &entry, line, carriedBy(id, spell), &noSources);
                        if (noSources) ++plan.entriesWithoutSources;
                        if (!target) return;
                        // In the list either way now: later lines' formListsNone see it
                        // (not a line measured while off - it changes nothing)
                        if (live) carriedCache[id].insert(ListMark(line.formList));
                        if (source.inFormList && source.inFormList(line.formList, id)) {
                            ++stats.agree;
                            return;
                        }
                        ++stats.gain;
                        AddExample(stats.gainExamples, SpellLabel(spell, id));
                        if (live) plan.listWrites.push_back({ f, l, line.formList, id });
                    });
                    continue;
                }

                // Which spells want the keyword, grouped by the effect it would go on.
                std::unordered_map<std::string, std::vector<std::string>> wanting;
                source.forEachCatalogSpell([&](const std::string& id, const json& spell, const json& entry) {
                    const auto& carried = carriedBy(id, spell);
                    bool noSources = false;
                    const auto target = PickTarget(spell, &entry, line, carried, &noSources);
                    if (noSources) ++plan.entriesWithoutSources;
                    if (!target) return;
                    if (carried.contains(line.keyword)) {
                        ++stats.agree;
                        return;
                    }
                    const json& effect = EffectsOf(spell)[*target];
                    if (HasConditions(effect)) {
                        ++stats.conditioned;
                        return;
                    }
                    const std::string key = EffectKey(effect);
                    if (key.empty()) return;
                    wanting[key].push_back(id);
                });

                for (const auto& [key, wanters] : wanting) {
                    // Leak check: every item using the effect has to pass on its own.
                    const auto users = source.usersOf(key);
                    const EffectUser* blocker = nullptr;
                    bool blockedByItem = false;
                    for (const auto& user : users) {
                        if (line.allowSharedEffects) break;   // "sharedEffects": "allow"
                        if (Contains(wanters, user.id)) continue;
                        if (!user.spell) {
                            blocker = &user;
                            blockedByItem = true;
                            break;
                        }
                        const auto& carried = carriedBy(user.id, *user.spell);
                        if (carried.contains(line.keyword)) continue;
                        // A spell using this effect as a perk bonus (a conditioned
                        // item) would get the keyword on that bonus: blocks
                        const bool bonusUse = std::ranges::any_of(EffectsOf(*user.spell),
                            [&](const json& e) { return EffectKey(e) == key && HasConditions(e); });
                        if (!bonusUse && PickTarget(*user.spell, user.catalogEntry, line, carried)) continue;
                        blocker = &user;
                        break;
                    }
                    if (blocker) {
                        ++stats.leaks;
                        if (blockedByItem) ++stats.blockedByItem;
                        AddExample(stats.leakExamples, key + " <- " + (blocker->spell ? SpellLabel(*blocker->spell, blocker->id) : blocker->id));
                        continue;
                    }

                    ++stats.writes;
                    if (live) {
                        plan.writes.push_back({ f, l, line.keyword, key });
                        effectKeywords[key].insert(line.keyword);
                    }
                    for (const auto& user : users) {
                        if (!user.spell) continue;
                        if (carriedBy(user.id, *user.spell).contains(line.keyword)) continue;
                        ++stats.gain;
                        AddExample(stats.gainExamples, SpellLabel(*user.spell, user.id));
                        if (live) carriedCache[user.id].insert(line.keyword);
                    }
                }
            }
        }

        // A catalog from before 1.6: the tierMin lines judged its elements as
        // framework. Said once per session - every plan would repeat it.
        static bool s_toldWithoutSources = false;
        if (plan.entriesWithoutSources > 0 && !s_toldWithoutSources) {
            s_toldWithoutSources = true;
            logger::warn("PerkAdapters: the catalog has no elementSources (written before 1.6) - {} spell checks of "
                         "tierMin lines took its elements as framework evidence; a full scan writes a new catalog",
                plan.entriesWithoutSources);
        }
        return plan;
    }

    json PlanReport(const std::vector<File>& files, const Plan& plan)
    {
        json report = json::array();
        for (std::size_t f = 0; f < files.size(); ++f) {
            json lines = json::array();
            for (std::size_t l = 0; l < files[f].lines.size(); ++l) {
                const Line& line = files[f].lines[l];
                const LineStats& s = plan.stats[f][l];
                lines.push_back({
                    { "keyword", line.keyword }, { "keywordForm", line.keywordForm }, { "formList", line.formList },
                    { "castingPerk", line.castingPerk }, { "damage", line.damageElements }, { "tierMin", line.tierMin },
                    { "enabled", line.enabled }, { "ran", s.ran },
                    { "agree", s.agree }, { "writes", s.writes }, { "gain", s.gain }, { "leaks", s.leaks },
                    { "conditioned", s.conditioned }, { "blockedByItem", s.blockedByItem },
                    { "gainExamples", s.gainExamples }, { "leakExamples", s.leakExamples },
                });
            }
            report.push_back({ { "file", files[f].name }, { "perkMod", files[f].perkMod },
                { "requiresConfig", files[f].requiresConfig }, { "lines", lines } });
        }
        return report;
    }
}
