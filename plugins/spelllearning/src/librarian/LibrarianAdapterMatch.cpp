#include "librarian/AdapterMatch.h"
#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"

#include <algorithm>
#include <string_view>

// =============================================================================
// LibrarianAdapterMatch - the adapter planners' condition checks
//
// Pure: no game types. The semantics are in PerkAdapters.h.
// =============================================================================

namespace Librarian::Adapters::Match
{
    namespace
    {
        // Field without the copy, for the checks the planner runs on every
        // spell for every line; the view lives as long as `object`
        std::string_view FieldView(const json& object, const char* key)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_string())
                ? std::string_view(found->get_ref<const std::string&>()) : std::string_view();
        }

        bool ContainsView(const std::vector<std::string>& list, std::string_view value)
        {
            return std::ranges::find(list, value) != list.end();
        }
    }

    std::string Field(const json& object, const char* key)
    {
        return std::string(FieldView(object, key));
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
        const std::string_view archetype = FieldView(effect, "archetype");
        const std::string_view primaryAV = FieldView(effect, "primaryAV");
        if (c.archetype && archetype != *c.archetype) return false;
        if (!c.archetypeAny.empty() && !ContainsView(c.archetypeAny, archetype)) return false;
        if (ContainsView(c.archetypeNone, archetype)) return false;
        if (c.primaryAV && primaryAV != *c.primaryAV) return false;
        if (!c.primaryAVAny.empty() && !ContainsView(c.primaryAVAny, primaryAV)) return false;
        if (c.resistance && FieldView(effect, "resistance") != *c.resistance) return false;
        if (c.detrimental && Flag(effect, "detrimental") != *c.detrimental) return false;
        if (c.delivery && FieldView(effect, "delivery") != *c.delivery) return false;
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
        if (!c.hazardSource.empty() && !ContainsView(c.hazardSource, FieldView(effect, "hazardSource"))) return false;
        if (c.explodes) {
            const auto projectile = effect.find("projectile");
            const bool value = projectile != effect.end() && projectile->is_object() && Flag(*projectile, "explodes");
            if (value != *c.explodes) return false;
        }
        if (c.visible && IsVisible(effect) != *c.visible) return false;
        return Detail::SummonedMatches(effect, c.summonedAny, c.summonedNone);
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

    std::string ListMark(const std::string& formList)
    {
        return "@list:" + formList;
    }

    int ElementRank(const json& catalogEntry, const std::string& element, bool& withoutSources)
    {
        const auto sources = catalogEntry.find("elementSources");
        if (sources == catalogEntry.end() || !sources->is_object()) {
            withoutSources = true;
            return TIER_RANK_FRAMEWORK;
        }
        const std::string tier = Field(*sources, element.c_str());
        if (tier.empty()) {
            withoutSources = true;   // a hand-edited entry: framework, like an old catalog
            return TIER_RANK_FRAMEWORK;
        }
        return TierRank(tier);
    }

    bool HasTag(const json& catalogEntry, const std::string& tag, std::optional<int> minRank, bool& withoutSources)
    {
        for (const char* axis : { "elements", "techniques" }) {
            const auto list = catalogEntry.find(axis);
            if (list == catalogEntry.end() || !list->is_array()) continue;
            for (const auto& value : *list) {
                if (!value.is_string() || value.get_ref<const std::string&>() != tag) continue;
                // Techniques have no per-tag tier; an element must rest on enough
                if (!minRank || std::string_view(axis) != "elements") return true;
                return ElementRank(catalogEntry, tag, withoutSources) >= *minRank;
            }
        }
        return false;
    }

    bool TagsMatch(const json& catalogEntry, const TagCondition& c, std::optional<int> minRank, bool& withoutSources)
    {
        for (const auto& tag : c.all) {
            if (!HasTag(catalogEntry, tag, minRank, withoutSources)) return false;
        }
        if (!c.any.empty() && std::ranges::none_of(c.any,
                [&](const auto& tag) { return HasTag(catalogEntry, tag, minRank, withoutSources); })) {
            return false;
        }
        bool ignored = false;
        return std::ranges::none_of(c.none, [&](const auto& tag) { return HasTag(catalogEntry, tag, std::nullopt, ignored); });
    }

    bool SpellMatches(const json& spell, const SpellCondition& c)
    {
        const std::string_view school = FieldView(spell, "school");
        if (c.school && school != *c.school) return false;
        if (c.notSchool && school == *c.notSchool) return false;
        if (c.tier && FieldView(spell, "skillLevel") != *c.tier) return false;
        if (c.casting && FieldView(spell, "castingType") != *c.casting) return false;
        if (c.twoHanded && Flag(spell, "twoHanded") != *c.twoHanded) return false;
        if (c.taughtByTome && Flag(spell, "taughtByTome") != *c.taughtByTome) return false;
        return true;
    }

    bool SpellLevelMatch(const json& spell, const json* catalogEntry, const Line& line,
        const std::unordered_set<std::string>& carried, bool& withoutSources)
    {
        if (catalogEntry && !TagsMatch(*catalogEntry, line.tags, line.tierMinRank, withoutSources)) return false;
        if (!SpellMatches(spell, line.spell)) return false;

        const json& effects = EffectsOf(spell);
        for (const auto& condition : line.spellHas) {
            if (std::ranges::none_of(effects, [&](const json& e) { return EffectMatches(e, condition); })) return false;
        }
        for (const auto& condition : line.spellLacks) {
            if (std::ranges::any_of(effects, [&](const json& e) { return EffectMatches(e, condition); })) return false;
        }
        for (const auto& keyword : line.keywordsNone) {
            if (carried.contains(keyword)) return false;
        }
        for (const auto& formList : line.formListsNone) {
            if (carried.contains(ListMark(formList))) return false;
        }
        return true;
    }
}
