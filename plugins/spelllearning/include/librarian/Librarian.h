#pragma once

#include "Common.h"

#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
    // A person looked at the spell and named what it is (a "spell" rule in a
    // manual file): for what the structure cannot say - blood, water or wind
    // magic dealing plain health damage through a script.
    inline constexpr const char* SOURCE_MANUAL = "manual";

    // How far the evidence behind a tag can be trusted to exist at all on
    // someone else's setup. MGEF structure is on every record in every load
    // order; a framework keyword is only there when that framework is
    // installed. This ranks availability, and is deliberately not a measured
    // probability that the tag is correct - for that see docs/librarian/MEASURED.md.
    inline constexpr double CONFIDENCE_MGEF = 1.0;
    inline constexpr double CONFIDENCE_FRAMEWORK = 0.8;
    // Keyed on the spell itself: present wherever the spell is.
    inline constexpr double CONFIDENCE_MANUAL = 1.0;
    inline constexpr double CONFIDENCE_NONE = 0.0;

    // How strong a tier is when two of them gave the same tag: the record
    // (mgef) and a person's reading (manual) over a framework's keyword. An
    // unknown tier from a user's file ranks below all three. Equal ranks keep
    // the piece of evidence whose rule comes first in merge order (mgef files
    // before manual ones).
    inline constexpr int TIER_RANK_MGEF = 3;
    inline constexpr int TIER_RANK_MANUAL = 2;
    inline constexpr int TIER_RANK_FRAMEWORK = 1;
    inline constexpr int TIER_RANK_UNKNOWN = 0;
    // From this rank up a tier is the record itself (mgef, manual): it can
    // contradict a framework keyword and is never contradicted. Below it
    // (framework, a user's own tier name) evidence can be contradicted.
    inline constexpr int TIER_RANK_RECORD = TIER_RANK_MANUAL;

    [[nodiscard]] int TierRank(const std::string& tier);

    // =========================================================================
    // EVIDENCE CONFLICTS
    // =========================================================================
    //
    // The elements the engine itself knows as damage, each with its own
    // resistance (FireResist, FrostResist, ElectricResist, PoisonResist). Only
    // these can be contradicted by a record: a damage effect resisted by one of
    // them, or marked as unspecified magic damage, is not another of them. Water,
    // air, earth, necrotic and the rest have no resistance of their own, so a
    // mod's spell of theirs is resisted by ResistMagic and nothing in the record
    // speaks against them.
    inline constexpr std::string_view ENGINE_DAMAGE_ELEMENTS[] = {
        "fire", "frost", "shock", "poison",
    };

    [[nodiscard]] bool IsEngineDamageElement(std::string_view element);

    // =========================================================================
    // TAGS
    // =========================================================================

    struct TagSet
    {
        std::set<std::string> elements;
        std::set<std::string> techniques;

        // Strongest tier behind any tag left on each axis (TierRank), after
        // every removal and the conflict stage; empty when the axis has no tags.
        // How portable one tag is reads from elementSources, not from this.
        std::string elementSource;
        std::string techniqueSource;

        // Strongest tier behind each element, one entry per element above.
        // (Techniques get no per-tag map: nothing reads one, and the conflict
        // stage never touches techniques.)
        std::map<std::string, std::string> elementSources;

        // Elements the conflict stage took off: every piece of their evidence
        // below the record tiers sat on an effect the spell's record contradicts.
        std::vector<std::string> conflictDropped;

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
        // The spell itself, by persistentId ("Plugin.esp|0x000D62"): one or
        // more. For hand-made rules, when nothing in the records says it.
        std::vector<std::string> spells;
        // The plugin the spell comes from, when its file name contains any of
        // these (case ignored): a themed mod says what its magic is by name -
        // "blood", "vampir" for a blood magic mod.
        std::vector<std::string> pluginContains;
        // The scanner's castByVampires: a vampire NPC carries the spell, in its
        // own spell list, its race's, or a leveled list in either.
        std::optional<bool> castByVampires;
        // "noElement": true - only for a spell no other rule gave an element,
        // after every removal: the complement of the element-tagged set. For a
        // mark that means "no element" (Bethesda's MagicDamageResist), which
        // is only evidence when nothing more specific is there.
        bool noElement = false;
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

        // Race keywords of what the effect summons (the scan's
        // summonedKeywords): "summonedAny" - at least one, "summonedNone" -
        // none of them. What a summon IS, as its race record says.
        std::vector<std::string> summonedAny;
        std::vector<std::string> summonedNone;

        // "targetRequires": the effect only takes hold on an actor with one of
        // these keywords (the scan's targetKeywords) - Turn Undead on
        // ActorTypeUndead. Who the spell is cast on, as its record says.
        std::vector<std::string> targetRequires;

        [[nodiscard]] bool HasEffectCondition() const;
        [[nodiscard]] bool Empty() const;
    };

    struct Rule
    {
        RuleMatch match;
        std::vector<std::string> addElements;
        std::vector<std::string> addTechniques;
        // "remove": tags taken off the spell after every rule has added its
        // own - for a hand-made rule correcting a tag the records got wrong
        // (a rune's shared blast effect reading as fire on a water rune)
        std::vector<std::string> removeElements;
        std::vector<std::string> removeTechniques;

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

        // MGEF keywords that say an effect deals magic damage of no engine
        // element (a rule file's top-level "unspecifiedDamageKeywords", merged
        // from every file). The conflict stage reads them: on an effect that
        // carries one, a framework keyword naming fire, frost, shock or poison
        // is contradicted by the record.
        std::vector<std::string> unspecifiedDamageKeywords;

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
    RuleSet LoadRules(const std::filesystem::path& directory);

    // Parses one "match" object into conditions. Shared with the keyword patch,
    // whose adapter entries carry the same conditions.
    RuleMatch ParseRuleMatch(const json& matchObject);

    // Parses one already-read rule document. A document may set a "tier" of its
    // own, which becomes the default for every rule in it - a whole file of
    // framework rules should not have to repeat itself. Exposed for tests and
    // for callers that keep rules somewhere other than a directory.
    void AppendRules(const json& document, const std::string& originFile, RuleSet& target);

    // Tags one spell object from the scan dump: every rule adds, then every
    // "remove" takes off, then the conflict stage takes away each piece of
    // evidence below the record tiers for an engine damage element whose
    // effect the record contradicts (that effect carries an
    // unspecifiedDamageKeywords mark, or mgef/manual rules gave that same
    // effect a different engine damage element; a cloak-like carrier that
    // deals no damage is judged by the damage it applies) and drops the
    // element when no evidence is left, then the noElement rules run (a tag
    // a "remove" took stays off). Sources are settled last, over the tags left.
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
        std::size_t conflictSpells = 0;   // entries the conflict stage changed
        std::map<std::string, std::size_t> conflictDropped;  // element -> entries that lost it
    };

    // Classifies every spell in a parsed scan dump. Pure: no file access, so
    // the offline harness builds the same catalog the game does.
    json BuildCatalog(const json& scanDump, const RuleSet& rules, CatalogStats& stats);

    // Loads the rules, builds the catalog for one parsed scan dump and writes
    // it. Returns false, leaving the file there alone, when the dump has
    // nothing to classify (no effects) or anything went wrong - a failure here
    // must never take the scan down with it, and when the file could not be
    // written (logged; the old file stays and the caller reads it back, so every
    // reader of the catalog agrees). On success `catalog` holds the new catalog.
    bool BuildAndWriteCatalog(const json& scanDump, json& catalog);

    // Reads the catalog back. Returns false when it is missing or unreadable.
    bool LoadCatalog(json& catalog);

    // =========================================================================
    // TREE TRAITS - the catalog handed on to the tree builder and spell card
    // =========================================================================
    //
    // The tree builder groups spells by their "traits" and the spell card shows
    // their "chips"; both used to know only the five elements vanilla keywords
    // name (fire, frost, shock, poison, disease). The catalog's elements replace
    // those, so a blood, water or holy spell - tagged by a framework or a manual
    // rule - groups with its kind, and a tag a rule removed is gone there too.
    //
    // Only what the magic is made of crosses over. The rest of the element axis
    // (creature, human, undead, armor, health, magicka, trap ...) says what a
    // spell acts on, which the scanner's kind.* traits already cover, and on
    // nearly every spell it would drown the rarer tags the bridges weigh.
    // Holy stays out too: nearly every holy spell is Restoration, so within a
    // school it groups nothing the school does not (decided 2026-09-27).
    inline constexpr std::string_view TREE_ELEMENTS[] = {
        "acid", "air", "arcane", "blood", "disease", "earth", "eldritch", "fire",
        "force", "frost", "light", "metal", "nature", "necrotic", "poison",
        "shadow", "shock", "soul", "sun", "time", "water",
    };

    // Spell Research files every conjured thing under soul. On a summon, bound
    // weapon or raised corpse that is the conjuring, not a theme; kind.summon
    // and the rest already group those, so soul is not handed on for them.
    inline constexpr std::string_view CONJURED_KINDS[] = {
        "kind.summon", "kind.bound", "kind.reanimate",
    };

    // The spell card shows at most this many chips (SpellScannerChips uses it
    // too, so the scanner and the merge agree).
    inline constexpr std::size_t MAX_CARD_CHIPS = 6;

    // Replaces the element.* traits and chips of every spell in a parsed scan
    // dump with that spell's catalog elements (those in TREE_ELEMENTS). Pure, so
    // the offline harness merges exactly as the game does. Returns how many
    // spells' element sets changed.
    std::size_t MergeCatalogElements(json& scanDump, const json& catalog);

    // What a caller does with a scan it has just produced: classifies it (or,
    // for a scan without effects, reads the catalog already there) and merges
    // the catalog's elements into scanJson in place. On any failure scanJson is
    // left exactly as it was. One classification at a time: with wait false
    // (a caller on the game thread) and another one running, this scan builds
    // no catalog and takes the one there is, as a tome scan does, instead of
    // stopping the game until the other is done.
    // Returns true when this call wrote a new spell_catalog.json; then
    // `onWritten`, when given, runs before the next classification can start
    // (the scan record, ScanRecord.h, is kept in step with the file that way).
    bool ClassifyScan(std::string& scanJson, bool wait = true, const std::function<void()>& onWritten = {});

    // The spell card builds its chips when a card opens, long after the scan,
    // so the catalog's elements are kept in memory for it: ClassifyScan stores
    // the catalog it used, and the first card before any scan this session
    // reads the file. Replaces the element chips of one spell like the merge
    // above does; leaves them alone when the catalog does not know the spell.
    void MergeCatalogChips(json& chips, const std::string& persistentId);
}
