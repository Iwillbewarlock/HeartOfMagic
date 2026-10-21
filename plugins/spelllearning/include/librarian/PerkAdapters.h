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
//     in {"any": [...]} is loaded, or any SKSE plugin in {"dlls": [...]} is in
//     Data/SKSE/Plugins (a mod with no .esp). A DLL on disk counts even when
//     SKSE refused to load it (wrong runtime): the file cannot tell
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
//     uses the effect as a conditioned item (a perk bonus) blocks as well.
//     "sharedEffects": "allow" (keyword lines only) skips this check: the effect
//     is written for the spells the line picks, whatever else uses it - scrolls,
//     enchantments and spells that use it as a perk bonus included (a mod that
//     reads the killing effect - FEC - wants the keyword on all of those)
//   - a written effect gives the keyword to every spell using it; later lines
//     see that
//   - "keywordForm" names a "keyword" line's keyword by FormID: in game the
//     loaded form's own editor id is used, whatever the last plugin called it
//   - a "formList" line puts each spell it picks into that FormList instead:
//     the same conditions, no leak check (the list names the spell itself), a
//     spell already in the list as its plugin wrote it is left alone
//   - formListsNone: no earlier live formList line of this plan picked the
//     spell for these lists. Entries a plugin or script made are not looked at
//   - a "castingPerk" line ("vanilla") gives each spell it picks that has no
//     half-cost perk of its own the vanilla one for its catalog school and tier
//     (DestructionNovice00 ...): the same conditions, no leak check (the perk
//     sits on the spell record). A spell with a perk of its own counts as agree
//   - "tierMin" ("mgef" | "manual" | "framework"): every element the line's tags
//     ask for (all: each one, any: the one that matches) must rest on evidence
//     of at least that rank in the catalog's elementSources (TierRank: mgef >
//     manual > framework). Techniques have no per-tag tier and are not checked.
//     A catalog entry without elementSources (written before 1.6), or with
//     no tier for that element, counts as framework
//   - a file with "requires": {"config": "a.b"} runs only while that config.json
//     boolean is not false (missing: on), on top of "always" / "any" / "dlls"
//   - a "damage" keyword line ({"elements": [...]}) plans per effect instead of
//     per spell (LibrarianAdapterDamage.cpp, "DAMAGE LINES" below)
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
        // Race keywords of what a summon calls up (the scan's summonedKeywords)
        std::vector<std::string> summonedAny;
        std::vector<std::string> summonedNone;
    };

    struct SpellCondition
    {
        std::optional<std::string> school;
        std::optional<std::string> notSchool;
        std::optional<std::string> tier;
        std::optional<std::string> casting;
        std::optional<bool> twoHanded;
        std::optional<bool> taughtByTome;   // the scan's mark: some tome teaches it
    };

    struct TagCondition
    {
        std::vector<std::string> all;
        std::vector<std::string> any;
        std::vector<std::string> none;
    };

    struct Line
    {
        // What the line gives a spell: a keyword on its effect, a place in a
        // perk mod's FormList ("formList": the list's persistent id), or a
        // half-cost perk ("castingPerk": "vanilla"). One of the three.
        std::string keyword;
        // The keyword by its persistent id ("Update.esm|0x1EA6002"), for a
        // keyword that mods inject into one FormID under different editor ids
        // (Ascension's DAR_UnspecificMagicDamage is Apostasy's
        // APO_MagicDamageUnspecified). In game the name the loaded form carries
        // replaces keyword before planning; offline the line keeps keyword.
        std::string keywordForm;
        std::string formList;
        std::string castingPerk;
        bool enabled = true;
        bool allowSharedEffects = false;   // "sharedEffects": "allow" - no leak check
        TagCondition tags;
        SpellCondition spell;
        std::optional<EffectCondition> effect;
        std::vector<EffectCondition> spellHas;
        std::vector<EffectCondition> spellLacks;
        std::vector<std::string> keywordsNone;
        // FormLists (persistent ids) that earlier formList lines of this plan
        // must not have picked the spell for
        std::vector<std::string> formListsNone;
        // Lowest evidence tier the tags' elements may rest on ("tierMin"),
        // as a TierRank; unset: any tier
        std::optional<int> tierMinRank;
        std::string tierMin;
        // "damage": {"elements": [...]} - the keyword goes on every effect of a
        // picked spell that deals damage of one of these elements (see
        // "DAMAGE LINES"). Empty: an ordinary line.
        std::vector<std::string> damageElements;
        // "damage": {"keywordsNone": [...]} - an effect carrying any of these
        // (a mark that says the damage is of no element) is not written
        std::vector<std::string> damageKeywordsNone;
    };

    struct File
    {
        std::string name;                   // file name, for the report
        std::string perkMod;
        bool always = false;                // requires {"always": true}
        std::vector<std::string> requiresAny;
        // requires {"dlls": [...]}: plain file names in Data/SKSE/Plugins
        std::vector<std::string> requiresDlls;
        // requires {"config": "section.key"}: a config.json boolean that must
        // not be false. Empty: no such gate.
        std::string requiresConfig;
        std::vector<Line> lines;
    };

    // Whether a dotted config path ("environment.enabled") is on in a parsed
    // config.json: true unless every part is there and the last one is the
    // boolean false - a player who never opens the settings gets the default.
    [[nodiscard]] bool ConfigFlagOn(const json& config, const std::string& dottedPath);

    // Whether an SKSE plugin file is in Data/SKSE/Plugins (relative to the game
    // folder, the working directory in game). Case-insensitive the way the
    // file system and MO2's VFS are. `name` is a plain file name (ParseFile
    // turns down anything with a path in it).
    [[nodiscard]] bool DllInstalled(const std::string& name);

    // Whether a file's requires holds: the config gate (if any) and then
    // "always", one of "any" loaded or one of "dlls" present. `pluginLoaded`
    // and `dllPresent` answer by file name.
    [[nodiscard]] bool FileApplies(const File& file, const json& config,
        const std::function<bool(const std::string&)>& pluginLoaded,
        const std::function<bool(const std::string&)>& dllPresent = DllInstalled);

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
    // `withoutSources`, when given, is set when a tierMin check met a catalog
    // entry without elementSources (taken as framework).
    std::optional<std::size_t> PickTarget(const json& spell, const json* catalogEntry,
        const Line& line, const std::unordered_set<std::string>& carried, bool* withoutSources = nullptr);

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

        // Whether the FormList exists in the load order, and whether a spell is
        // already in it as its plugin wrote it. Unset: always / never.
        std::function<bool(const std::string& formList)> formListDefined;
        std::function<bool(const std::string& formList, const std::string& spellId)> inFormList;

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

    // A spell to add to a FormList.
    struct ListWrite
    {
        std::size_t file = 0;
        std::size_t line = 0;
        std::string formList;
        std::string spellId;
    };

    // A half-cost perk for a spell that has none.
    struct CastingPerkWrite
    {
        std::size_t file = 0;
        std::size_t line = 0;
        std::string perk;               // persistent id
        std::string spellId;
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
        std::vector<ListWrite> listWrites;
        std::vector<CastingPerkWrite> castingPerkWrites;
        std::vector<std::vector<LineStats>> stats;   // [file][line]
        // Catalog entries a tierMin line read without elementSources (a
        // catalog written before 1.6): taken as framework. Counted once per
        // entry and line.
        std::size_t entriesWithoutSources = 0;
    };

    Plan BuildPlan(const std::vector<File>& files, const PlanSource& source);

    // =========================================================================
    // DAMAGE LINES
    // =========================================================================
    //
    // A "damage" keyword line gives the keyword to effects, not spells: every
    // effect (hidden ones too, not what a cloak applies) of a picked catalog
    // spell that deals damage of the line's elements. The spell is picked by
    // the line's spell level conditions (tags with tierMin, spell, spellHas,
    // spellLacks, keywordsNone, formListsNone) and by having at least one of
    // the line's elements in its catalog at tierMin - the matched elements M.
    // An effect deals that damage when:
    //   - it is detrimental with magnitude > 0 (this spell's entry), and its
    //     archetype is ValueModifier, DualValueModifier or PeakValueModifier on
    //     Health, or Script with the resistance of an element in M (scripted
    //     damage has no actor value to read). Absorb is not damage of an element
    //   - its own record says M: its resistance is the engine resistance of an
    //     element in M (FireResist, FrostResist, ElectricResist, PoisonResist -
    //     "resisted as M"). A neutral resistance (None, MagicResist) is no word
    //     on fire, frost, shock or poison - in a fire spell it is the tentacle,
    //     arcane or sun part - so it counts only for an element with no
    //     resistance of its own (water, air ...) in M, on a visible effect, in a
    //     spell the catalog gives no engine damage element outside M. Any
    //     other resistance (another element's, DamageResist ...) never counts
    //   - a hidden Script effect never counts (a perk's fear or disintegrate
    //     check resisted by FireResist): the librarian takes hidden effects as
    //     evidence only when they change a value, and so does this
    //   - it carries none of the line's damage.keywordsNone, matches the line's
    //     "effect" block, if any, and its item has no conditions (a perk bonus)
    // An effect that already has the keyword (from its plugin or an earlier
    // line) is left alone. An effect is written only when every item using it
    // is a catalog spell this line picks for which that same effect's record
    // passes the tests above (so the same elements at tierMin; that spell's
    // own magnitude and conditions do not matter - a fire spell using the fire
    // effect as a perk bonus, or at magnitude 0, still uses fire damage).
    // Another spell, an enchantment, scroll, potion or ingredient blocks it,
    // and is named in the leak examples. An effect only ever used with
    // conditions is not written (counted in "conditioned").
    // "sharedEffects": "allow" skips that check. Stats: agree - picked spells
    // whose damage effects (items with conditions aside) all have the keyword
    // already; writes - effects; gain - spells that carry the keyword nowhere
    // before and now do; conditioned - effects lacking the keyword that picked
    // spells use only with conditions.

    // Effects (key) that have each keyword now: what the plugins wrote plus
    // what earlier lines of this plan wrote.
    using EffectKeywords = std::unordered_map<std::string, std::unordered_set<std::string>>;

    void PlanDamageLine(std::size_t fileIndex, std::size_t lineIndex, const Line& line, bool live,
        const PlanSource& source, EffectKeywords& effectKeywords,
        std::unordered_map<std::string, std::unordered_set<std::string>>& carriedCache,
        Plan& plan);

    // Report of a plan, the shape both the game and the harness write.
    json PlanReport(const std::vector<File>& files, const Plan& plan);
}
