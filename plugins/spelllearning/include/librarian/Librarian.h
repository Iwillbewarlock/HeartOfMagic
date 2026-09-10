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

    // How far the evidence behind a tag can be trusted to exist at all on
    // someone else's setup. MGEF structure is on every record in every load
    // order; a framework keyword is only there when that framework is
    // installed. This ranks availability, and is deliberately not a measured
    // probability that the tag is correct - for that see docs/librarian/MEASURED.md.
    inline constexpr double CONFIDENCE_MGEF = 1.0;
    inline constexpr double CONFIDENCE_FRAMEWORK = 0.8;
    inline constexpr double CONFIDENCE_NONE = 0.0;

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
        std::string spellKeywordSuffix;

        // Effect level - all of these must hold for one and the same effect
        std::string mgefKeyword;
        std::string mgefKeywordPrefix;

        // The frameworks put the subject in the prefix and the shape in the
        // suffix: ADAR_SPEL_Earth_Rune is earth by prefix and a trap by suffix.
        std::string mgefKeywordSuffix;
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

    // Parses one "match" object into conditions. Shared with the keyword patch,
    // whose adapter entries carry the same conditions.
    RuleMatch ParseRuleMatch(const json& matchObject);

    // Parses one already-read rule document. A document may set a "tier" of its
    // own, which becomes the default for every rule in it - a whole file of
    // framework rules should not have to repeat itself. Exposed for tests and
    // for callers that keep rules somewhere other than a directory.
    void AppendRules(const json& document, const std::string& originFile, RuleSet& target);

    // Tags one spell object from the scan dump.
    TagSet Classify(const json& spell, const RuleSet& rules);

    // Whether one effect satisfies a rule's effect level conditions. Exposed
    // for the keyword patch, which asks the same question of a single effect.
    bool EffectMatches(const json& effect, const RuleMatch& match);

    // =========================================================================
    // AXES
    // =========================================================================
    //
    // The four axes fall out of scan fields with no rules involved: school and
    // tier are the scanner's own values lowercased, casting and targeting are
    // read off the delivery and the effect areas. The derivation is the one
    // checked against the answer set in HANDOFF section 3.

    struct SpellAxes
    {
        std::string school;                // alteration | conjuration | destruction | illusion | restoration
        std::string tier;                  // novice | apprentice | adept | expert | master
        std::string casting;               // fireforget | concentration
        std::set<std::string> targeting;   // actor | aoe | location | self, more than one is normal
    };

    SpellAxes DeriveAxes(const json& spell);

    // =========================================================================
    // CATALOG
    // =========================================================================
    //
    // The catalog is the librarian's output and the single thing every guest
    // reads: persistentId -> axes, tags, which evidence tier supplied them.
    // Written next to the scan dump, in Data/SKSE/Plugins/SpellLearning.

    inline constexpr int CATALOG_VERSION = 1;
    inline constexpr const char* CATALOG_VOCAB = "tags-v1";

    struct CatalogStats
    {
        std::size_t spells = 0;      // entries written
        std::size_t tagged = 0;      // entries with at least one tag
        std::size_t skipped = 0;     // scan entries with no persistentId to key on
    };

    // Classifies every spell in a parsed scan dump. Pure: no file access, so
    // the offline harness builds the same catalog the game does.
    json BuildCatalog(const json& scanDump, const RuleSet& rules, CatalogStats& stats);

    // Loads the rules, builds the catalog for one scan dump and writes it.
    // Takes the dump as the scanner's own JSON text so a caller that has just
    // produced one does not have to parse it first. Returns the written path,
    // or an empty string if anything went wrong - a failure here must never
    // take the scan down with it.
    std::string BuildAndWriteCatalog(const std::string& scanJson);

    // Reads the catalog back. Returns false when it is missing or unreadable.
    bool LoadCatalog(json& catalog);

    // =========================================================================
    // VANILLA KEYWORD PATCH
    // =========================================================================
    //
    // Perk mods gate their bonuses on the vanilla keywords - Adamant, Ordinator
    // and vanilla itself all ask HasMagicEffectKeyword MagicDamageFire. A modded
    // fire spell whose MGEF never got that keyword is invisible to every one of
    // them. The librarian knows the effect is fire, so it can fill the keyword
    // in, and a whole category of "this mod's spells ignore my perks" goes away
    // without anyone writing a patch.
    //
    // Rules of engagement, in order of importance:
    //   - keywords are only ever ADDED, never removed
    //   - only keywords that already exist in the load order are added
    //   - an effect that already has the keyword is left alone
    //   - plugins the user lists are skipped entirely
    //   - the whole thing is one config switch away from being off
    //
    // This edits runtime form data, which is not written to saves, so removing
    // the mod removes the change.

    struct KeywordPatchStats
    {
        std::size_t effectsSeen = 0;              // base effects worth examining
        std::size_t effectsPatched = 0;           // effects that gained at least one keyword
        std::size_t keywordsAdded = 0;            // keywords added in total
        std::size_t keywordsAlreadyPresent = 0;   // adapter hits the effect already satisfied
        std::size_t excluded = 0;                 // effects skipped, plugin is excluded
    };

    // Reads librarian/adapter_vanilla_keywords.json and the classification
    // rules, then walks every base effect in the load order. Safe to call when
    // the feature is off or the adapter file is missing - it does nothing and
    // says so. Call after kDataLoaded, on the game thread.
    KeywordPatchStats ApplyVanillaKeywordPatch();

    // What the last patch run did, zeroed if it never ran. A scan taken after
    // the patch sees keywords that are not in anyone's plugin files, so the
    // dump records this and anything measuring off that dump can say so.
    KeywordPatchStats LastKeywordPatchStats();
}
