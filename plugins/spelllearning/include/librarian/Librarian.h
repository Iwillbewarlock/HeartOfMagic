#pragma once

#include "Common.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;

// =============================================================================
// Librarian - tags a scanned spell from its MGEF structure
//
// The classifier is pure: it reads a spell object from the scan dump and a set
// of rules loaded from JSON, and returns tags. It touches no game state, so the
// same code runs inside the plugin and in the offline tools/librarian-test
// harness. Nothing here may use RE:: or SKSE:: types.
//
// Rules live in data, not code (SKSE/Plugins/SpellLearning/librarian/*.json),
// so a user or another modder can add classifications without a patch.
// =============================================================================

namespace Librarian
{
    // =========================================================================
    // EVIDENCE TIERS
    // =========================================================================
    //
    // Which kind of evidence produced a tag. Ordered strongest first: MGEF
    // structure is present in every load order, framework keywords only when
    // that framework is installed. A spell's source is the strongest tier that
    // contributed to it, so the catalog records what the tag actually rests on.

    inline constexpr const char* SOURCE_MGEF = "mgef";
    inline constexpr const char* SOURCE_FRAMEWORK = "framework";

    // =========================================================================
    // TAGS
    // =========================================================================

    struct TagSet
    {
        std::set<std::string> elements;
        std::set<std::string> techniques;

        // Strongest tier that contributed to each axis, empty when the axis got
        // no tags at all.
        std::string elementSource;
        std::string techniqueSource;

        [[nodiscard]] bool Empty() const
        {
            return elements.empty() && techniques.empty();
        }
    };

    // =========================================================================
    // RULES
    // =========================================================================
    //
    // A rule matches when every condition it names holds. Spell level
    // conditions are checked against the spell, effect level conditions against
    // one single effect - a rule naming both an archetype and a resistance
    // wants them on the same effect, not scattered across the spell.
    //
    // {
    //   "match": { "mgefKeyword": "MagicDamageFire" },
    //   "add":   { "elements": ["fire"] }
    // }

    struct RuleMatch
    {
        // Spell level
        std::string spellKeyword;
        std::string spellKeywordPrefix;

        // Effect level - all of these must hold for one and the same effect
        std::string mgefKeyword;
        std::string mgefKeywordPrefix;
        std::string archetype;
        std::string primaryAV;
        std::string secondaryAV;
        std::string resistance;
        std::string magicSkill;

        // The value modifier archetypes cover both halves of every pair -
        // damage and restore, weaken and fortify - and only these flags tell
        // them apart. Unset means the rule does not care.
        std::optional<bool> hostile;
        std::optional<bool> detrimental;

        [[nodiscard]] bool HasEffectCondition() const;
        [[nodiscard]] bool Empty() const;
    };

    struct Rule
    {
        RuleMatch match;
        std::vector<std::string> addElements;
        std::vector<std::string> addTechniques;

        // Evidence tier, from the rule's "tier" field. Defaults to mgef.
        std::string source;

        // Where this came from, for diagnostics only.
        std::string originFile;
        std::size_t originIndex = 0;
    };

    struct RuleSet
    {
        std::vector<Rule> rules;

        // Files that were read, in the order they were merged.
        std::vector<std::string> files;

        // Rules skipped because they had no conditions or added no tags.
        std::size_t skipped = 0;

        // Tags dropped because they are not in the vocabulary. A typo in a
        // user's rule file lands here rather than inventing a tag no adapter
        // can translate, so a non-zero count means a rule file needs fixing.
        std::size_t rejectedTags = 0;
    };

    // =========================================================================
    // API
    // =========================================================================

    // Loads every *.json in a directory, merged in file name order so that
    // 00_mgef.json lands before 10_kit.json and 90_user.json comes last.
    // Missing directory is not an error - it yields an empty rule set.
    RuleSet LoadRules(const std::string& directory);

    // Parses one already-read rule document. Exposed for tests and for callers
    // that keep rules somewhere other than a directory.
    void AppendRules(const json& document, const std::string& originFile, RuleSet& target);

    // Tags one spell object from the scan dump.
    TagSet Classify(const json& spell, const RuleSet& rules);
}
