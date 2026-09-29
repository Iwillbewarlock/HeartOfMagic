#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// Perk adapters - which spells get which perk mod keyword
//
// Perk overhauls recognise spells by keywords on the spells' magic effects
// (EPMagic_SpellHasKeyword MagicDamageFire ...). A modded spell whose effects
// lack the keyword is invisible to the perk. An adapter file per perk mod
// (SKSE/Plugins/SpellLearning/librarian/adapters/*.json) says which keyword a
// spell should carry, in terms of the HoM scan and the librarian's catalog tags
// only - no spell, effect or plugin is ever named. docs/librarian/PERK_ADAPTERS.md
// has the format and the measurements behind every line.
//
// This part is pure: it reads spell objects shaped like the scan dump and
// catalog entries, and plans the writes. The game side (PerkAdapterPatch.h)
// feeds it the load order and applies the plan; tools/librarian-test feeds it
// a scan dump and prints the same plan, so the two can be compared.
//
// SEMANTICS (the offline evaluator measured exactly these):
//   - files run vanilla.json first, then by name; lines in file order
//   - a file runs when its "requires" holds: {"always": true}, or any plugin
//     in {"any": [...]} is loaded
//   - a line is skipped when "enabled" is false or its keyword is not defined
//   - catalog spells only. A spell that already carries the keyword - on the
//     spell record, on any effect, or added by an earlier line - is left alone
//   - otherwise every condition must hold: tags (catalog elements/techniques,
//     all/any/none), spell (scan spell fields), spellHas / spellLacks (some / no
//     effect, hidden ones too), keywordsNone (none of these on the spell,
//     counting earlier lines)
//   - the target is the first VISIBLE effect (not hidden in the UI) that
//     matches "effect", or the first visible effect when the line has none. A
//     target whose effect item has conditions (a perk bonus) is skipped
//   - an effect is written only when every item using it passes: a catalog
//     spell the line picks or that carries the keyword; a spell outside the
//     catalog that meets every condition except the tags; nothing else (an
//     enchantment, scroll or potion using it blocks the write). A spell that
//     uses the effect as a conditioned item (a perk bonus) blocks as well
//   - a written effect gives the keyword to every spell using it; later lines
//     see that
// =============================================================================

namespace Librarian::Adapters
{
    using json = nlohmann::json;

    // =========================================================================
    // ADAPTER FILES
    // =========================================================================

    // One effect's conditions. Unset fields do not constrain.
    struct EffectCondition
    {
        std::optional<std::string> archetype;
        std::vector<std::string> archetypeAny;
        std::vector<std::string> archetypeNone;
        std::optional<std::string> primaryAV;
        std::vector<std::string> primaryAVAny;
        std::optional<std::string> resistance;
        std::optional<bool> detrimental;
        std::optional<std::string> delivery;
        std::optional<double> minDuration;
        std::optional<bool> noDuration;
        std::vector<std::string> hazardSource;   // any of: impact | effect | explosion
        std::optional<bool> explodes;
        std::optional<bool> visible;
    };

    struct SpellCondition
    {
        std::optional<std::string> school;
        std::optional<std::string> notSchool;
        std::optional<std::string> tier;
        std::optional<std::string> casting;
        std::optional<bool> twoHanded;
    };

    struct TagCondition
    {
        std::vector<std::string> all;
        std::vector<std::string> any;
        std::vector<std::string> none;
    };

    struct Line
    {
        std::string keyword;
        bool enabled = true;
        TagCondition tags;
        SpellCondition spell;
        std::optional<EffectCondition> effect;
        std::vector<EffectCondition> spellHas;
        std::vector<EffectCondition> spellLacks;
        std::vector<std::string> keywordsNone;
    };

    struct File
    {
        std::string name;                   // file name, for the report
        std::string perkMod;
        bool always = false;                // requires {"always": true}
        std::vector<std::string> requiresAny;
        std::vector<Line> lines;
    };

    // Parses one adapter document. A line with a key this code does not know
    // is kept but disabled, and every problem is appended to `problems` - an
    // unknown condition silently matching everything would be worse than the
    // line doing nothing. Returns false when the document is not an adapter
    // file at all.
    bool ParseFile(const json& document, const std::string& name, File& file,
        std::vector<std::string>& problems);

    // vanilla.json first, then by name - the order keywordsNone is written for.
    void SortFiles(std::vector<File>& files);

    // =========================================================================
    // MATCHING
    // =========================================================================

    // Keywords a scanned spell carries: its own and every effect's.
    std::unordered_set<std::string> CarriedKeywords(const json& spell);

    // How an effect is identified: its "form" (persistent id) when the scan
    // wrote one, else its editor id. Empty when it has neither.
    std::string EffectKey(const json& effect);

    // Every condition of the line, the tags included when `catalogEntry` is
    // given (otherwise the tags are not checked: a spell outside the catalog
    // is judged on its structure). `carried` must already include keywords
    // added by earlier lines; the "already carries the keyword" skip is the
    // caller's. Returns the index into spell["effects"] of the target effect.
    std::optional<std::size_t> PickTarget(const json& spell, const json* catalogEntry,
        const Line& line, const std::unordered_set<std::string>& carried);

    // =========================================================================
    // PLAN
    // =========================================================================

    // An item using an effect. `spell` is null for anything that is not a
    // spell (enchantment, scroll, potion, ingredient): those block writes.
    struct EffectUser
    {
        std::string id;                 // persistent id
        const json* spell = nullptr;    // scan-shaped spell object
        const json* catalogEntry = nullptr;
    };

    struct PlanSource
    {
        // Every catalog spell, scan-shaped, with its catalog entry.
        std::function<void(const std::function<void(const std::string& id, const json& spell,
            const json& catalogEntry)>&)> forEachCatalogSpell;

        // Every item that uses the effect with this key.
        std::function<std::vector<EffectUser>(const std::string& effectKey)> usersOf;

        // Whether the load order defines the keyword. Unset: always.
        std::function<bool(const std::string& keyword)> keywordDefined;

        // Measure disabled lines too, without letting them change anything.
        bool measureDisabled = false;
    };

    struct Write
    {
        std::size_t file = 0;
        std::size_t line = 0;
        std::string keyword;
        std::string effectKey;
    };

    struct LineStats
    {
        bool ran = false;               // enabled and its keyword defined
        std::size_t agree = 0;          // picked, and already carried the keyword
        std::size_t writes = 0;         // effects written
        std::size_t gain = 0;           // spells that now carry the keyword
        std::size_t leaks = 0;          // effect writes dropped by the leak check
        std::size_t conditioned = 0;    // targets skipped: the effect item has conditions
        std::size_t blockedByItem = 0;  // of the leaks, those an enchantment/scroll/potion caused
        std::vector<std::string> gainExamples;
        std::vector<std::string> leakExamples;
    };

    struct Plan
    {
        std::vector<Write> writes;
        std::vector<std::vector<LineStats>> stats;   // [file][line]
    };

    Plan BuildPlan(const std::vector<File>& files, const PlanSource& source);

    // Report of a plan, the shape both the game and the harness write.
    json PlanReport(const std::vector<File>& files, const Plan& plan);
}
