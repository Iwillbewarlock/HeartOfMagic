#include "librarian/PerkAdapters.h"

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
    namespace
    {
        constexpr const char* VANILLA_FILE = "vanilla.json";
        constexpr std::size_t MAX_EXAMPLES = 8;

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
            read.RejectUnknown({ "archetype", "archetypeAny", "archetypeNone", "primaryAV", "primaryAVAny",
                "resistance", "detrimental", "delivery", "minDuration", "noDuration", "hazardSource",
                "explodes", "visible" });
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
            read.Optional("keyword", keyword, &json::is_string);
            if (!keyword || keyword->empty()) {
                problems.push_back(where + ": no keyword");
                return false;
            }
            line.keyword = *keyword;

            std::optional<bool> enabled;
            read.Optional("enabled", enabled, &json::is_boolean);
            line.enabled = enabled.value_or(true);
            read.List("keywordsNone", line.keywordsNone);

            bool ok = true;
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
                    spellRead.RejectUnknown({ "school", "notSchool", "tier", "casting", "twoHanded" });
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

            read.RejectUnknown({ "keyword", "why", "enabled", "tags", "spell", "effect", "spellHas",
                "spellLacks", "keywordsNone" });
            return read.ok && ok;
        }

        // ---------------------------------------------------------------------
        // Matching
        // ---------------------------------------------------------------------

        std::string Field(const json& object, const char* key)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_string()) ? found->get<std::string>() : std::string();
        }

        bool Flag(const json& object, const char* key)
        {
            const auto found = object.find(key);
            return found != object.end() && found->is_boolean() && found->get<bool>();
        }

        bool Contains(const std::vector<std::string>& list, const std::string& value)
        {
            return std::ranges::find(list, value) != list.end();
        }

        bool IsVisible(const json& effect)
        {
            const auto flags = effect.find("flags");
            return !(flags != effect.end() && flags->is_object() && Flag(*flags, "hideInUI"));
        }

        bool HasConditions(const json& effect)
        {
            const auto found = effect.find("conditions");
            return found != effect.end() && found->is_number() && found->get<double>() > 0;
        }

        bool EffectMatches(const json& effect, const EffectCondition& c)
        {
            const std::string archetype = Field(effect, "archetype");
            const std::string primaryAV = Field(effect, "primaryAV");
            if (c.archetype && archetype != *c.archetype) return false;
            if (!c.archetypeAny.empty() && !Contains(c.archetypeAny, archetype)) return false;
            if (Contains(c.archetypeNone, archetype)) return false;
            if (c.primaryAV && primaryAV != *c.primaryAV) return false;
            if (!c.primaryAVAny.empty() && !Contains(c.primaryAVAny, primaryAV)) return false;
            if (c.resistance && Field(effect, "resistance") != *c.resistance) return false;
            if (c.detrimental && Flag(effect, "detrimental") != *c.detrimental) return false;
            if (c.delivery && Field(effect, "delivery") != *c.delivery) return false;
            if (c.minDuration) {
                const auto duration = effect.find("duration");
                const double value = (duration != effect.end() && duration->is_number()) ? duration->get<double>() : 0.0;
                if (value < *c.minDuration) return false;
            }
            if (c.noDuration) {
                const auto flags = effect.find("flags");
                const bool value = flags != effect.end() && flags->is_object() && Flag(*flags, "noDuration");
                if (value != *c.noDuration) return false;
            }
            if (!c.hazardSource.empty() && !Contains(c.hazardSource, Field(effect, "hazardSource"))) return false;
            if (c.explodes) {
                const auto projectile = effect.find("projectile");
                const bool value = projectile != effect.end() && projectile->is_object() && Flag(*projectile, "explodes");
                if (value != *c.explodes) return false;
            }
            if (c.visible && IsVisible(effect) != *c.visible) return false;
            return true;
        }

        bool HasTag(const json& catalogEntry, const std::string& tag)
        {
            for (const char* axis : { "elements", "techniques" }) {
                const auto list = catalogEntry.find(axis);
                if (list == catalogEntry.end() || !list->is_array()) continue;
                for (const auto& value : *list) {
                    if (value.is_string() && value.get<std::string>() == tag) return true;
                }
            }
            return false;
        }

        bool TagsMatch(const json& catalogEntry, const TagCondition& c)
        {
            for (const auto& tag : c.all) {
                if (!HasTag(catalogEntry, tag)) return false;
            }
            if (!c.any.empty() && std::ranges::none_of(c.any, [&](const auto& tag) { return HasTag(catalogEntry, tag); })) {
                return false;
            }
            return std::ranges::none_of(c.none, [&](const auto& tag) { return HasTag(catalogEntry, tag); });
        }

        bool SpellMatches(const json& spell, const SpellCondition& c)
        {
            const std::string school = Field(spell, "school");
            if (c.school && school != *c.school) return false;
            if (c.notSchool && school == *c.notSchool) return false;
            if (c.tier && Field(spell, "skillLevel") != *c.tier) return false;
            if (c.casting && Field(spell, "castingType") != *c.casting) return false;
            if (c.twoHanded && Flag(spell, "twoHanded") != *c.twoHanded) return false;
            return true;
        }

        const json& EffectsOf(const json& spell)
        {
            static const json kEmpty = json::array();
            const auto found = spell.find("effects");
            return (found != spell.end() && found->is_array()) ? *found : kEmpty;
        }

        void AddExample(std::vector<std::string>& examples, std::string text)
        {
            if (examples.size() < MAX_EXAMPLES) examples.push_back(std::move(text));
        }

        std::string SpellLabel(const json& spell, const std::string& id)
        {
            const std::string name = Field(spell, "name");
            return name.empty() ? id : name + " [" + id + "]";
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
            read.RejectUnknown({ "always", "any" });
        }

        std::size_t index = 0;
        for (const auto& entry : document["adapters"]) {
            Line line;
            const std::string where = name + " line " + std::to_string(index + 1);
            if (!ParseLine(entry, where, line, problems)) {
                line.enabled = false;
            }
            if (!line.keyword.empty()) file.lines.push_back(std::move(line));
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
        const std::unordered_set<std::string>& carried)
    {
        if (catalogEntry && !TagsMatch(*catalogEntry, line.tags)) return std::nullopt;
        if (!SpellMatches(spell, line.spell)) return std::nullopt;

        const json& effects = EffectsOf(spell);
        for (const auto& condition : line.spellHas) {
            if (std::ranges::none_of(effects, [&](const json& e) { return EffectMatches(e, condition); })) return std::nullopt;
        }
        for (const auto& condition : line.spellLacks) {
            if (std::ranges::any_of(effects, [&](const json& e) { return EffectMatches(e, condition); })) return std::nullopt;
        }
        for (const auto& keyword : line.keywordsNone) {
            if (carried.contains(keyword)) return std::nullopt;
        }

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
                const bool defined = !source.keywordDefined || source.keywordDefined(line.keyword);
                const bool live = line.enabled && defined;
                stats.ran = live;
                if (!live && !(source.measureDisabled && defined)) continue;

                // Which spells want the keyword, grouped by the effect it would go on.
                std::unordered_map<std::string, std::vector<std::string>> wanting;
                source.forEachCatalogSpell([&](const std::string& id, const json& spell, const json& entry) {
                    const auto& carried = carriedBy(id, spell);
                    const auto target = PickTarget(spell, &entry, line, carried);
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
                    if (live) plan.writes.push_back({ f, l, line.keyword, key });
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
                    { "keyword", line.keyword }, { "enabled", line.enabled }, { "ran", s.ran },
                    { "agree", s.agree }, { "writes", s.writes }, { "gain", s.gain }, { "leaks", s.leaks },
                    { "conditioned", s.conditioned }, { "blockedByItem", s.blockedByItem },
                    { "gainExamples", s.gainExamples }, { "leakExamples", s.leakExamples },
                });
            }
            report.push_back({ { "file", files[f].name }, { "perkMod", files[f].perkMod }, { "lines", lines } });
        }
        return report;
    }
}
