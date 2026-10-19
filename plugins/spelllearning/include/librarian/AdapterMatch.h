#pragma once

#include "librarian/PerkAdapters.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// =============================================================================
// AdapterMatch - the condition checks the adapter planners share
//
// Pure, like PerkAdapters.h: the per-spell planner (LibrarianAdapters.cpp) and
// the per-effect damage planner (LibrarianAdapterDamage.cpp) read spells and
// catalog entries through these. Not part of the public API - nothing outside
// src/librarian/ and the offline harness should include this.
// =============================================================================

namespace Librarian::Adapters::Match
{
    inline constexpr std::size_t MAX_EXAMPLES = 8;

    [[nodiscard]] std::string Field(const json& object, const char* key);
    [[nodiscard]] bool Flag(const json& object, const char* key);
    [[nodiscard]] bool Contains(const std::vector<std::string>& list, const std::string& value);

    [[nodiscard]] bool IsVisible(const json& effect);
    // The effect item has conditions: a perk bonus riding in the spell
    [[nodiscard]] bool HasConditions(const json& effect);
    [[nodiscard]] bool EffectMatches(const json& effect, const EffectCondition& c);
    [[nodiscard]] const json& EffectsOf(const json& spell);

    void AddExample(std::vector<std::string>& examples, std::string text);
    [[nodiscard]] std::string SpellLabel(const json& spell, const std::string& id);

    // A formList line's pick, kept among the spell's carried keywords so
    // formListsNone reads it like keywordsNone. No editor id has a '@'.
    [[nodiscard]] std::string ListMark(const std::string& formList);

    // The TierRank of the evidence behind one catalog element. An entry with
    // no elementSources (a catalog from before 1.6) ranks as framework and
    // sets `withoutSources`.
    [[nodiscard]] int ElementRank(const json& catalogEntry, const std::string& element, bool& withoutSources);

    // Whether a tag is on the entry, and - when it is an element and a rank is
    // asked for - rests on evidence of at least that rank.
    [[nodiscard]] bool HasTag(const json& catalogEntry, const std::string& tag,
        std::optional<int> minRank, bool& withoutSources);

    // tags all / any / none. tierMin applies to all and any; a tag in none
    // excludes the spell whatever its tier.
    [[nodiscard]] bool TagsMatch(const json& catalogEntry, const TagCondition& c,
        std::optional<int> minRank, bool& withoutSources);

    [[nodiscard]] bool SpellMatches(const json& spell, const SpellCondition& c);

    // Every spell level condition of a line: tags (when `catalogEntry` is
    // given), spell, spellHas, spellLacks, keywordsNone, formListsNone.
    [[nodiscard]] bool SpellLevelMatch(const json& spell, const json* catalogEntry, const Line& line,
        const std::unordered_set<std::string>& carried, bool& withoutSources);
}
