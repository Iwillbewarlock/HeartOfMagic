#pragma once

#include "Common.h"
#include "ScanRecordRules.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SpellScanner
{
    using json = nlohmann::json;
    // Field output configuration
    struct FieldConfig {
        bool editorId = true;
        bool magickaCost = true;
        bool minimumSkill = false;
        bool castingType = false;
        bool delivery = false;
        bool chargeTime = false;
        bool plugin = false;
        bool effects = false;
        bool effectNames = false;
        bool keywords = false;
        // MGEF structure per effect: keywords, archetype, actor values, resistance.
        // Only has an effect when effects is also on. This is the evidence the
        // librarian classifies on, so it is off unless asked for.
        bool effectDetails = false;
    };

    // Scan configuration (fields + user prompt)
    struct ScanConfig {
        FieldConfig fields;
        std::string treeRulesPrompt;
    };

    // Parse scan config from JSON string (includes fields and treeRulesPrompt)
    ScanConfig ParseScanConfig(const std::string& jsonConfig);

    // Parse field config from JSON string (legacy support)
    FieldConfig ParseFieldConfig(const std::string& jsonConfig);

    // Scan all spells and return JSON output with spell data + prompts
    std::string ScanAllSpells(const ScanConfig& config);
    std::string ScanAllSpells(const FieldConfig& config = FieldConfig{});

    // Scan spells via spell tomes (avoids duplicates, only learnable spells)
    std::string ScanSpellTomes(const ScanConfig& config);

    // The game-data half of the two scans above: every form read happens in
    // here, so call these on the game thread. The scans above are
    // JsonText::Dump(<this>, 2); the panel's scan does that dump on a worker
    // thread (UIManagerScanner.cpp), so the text is the same either way.
    json CollectAllSpells(const ScanConfig& config);
    json CollectSpellTomes(const ScanConfig& config);

    // CollectAllSpells in pieces, for a scan that must not stop the game
    // (AutoScan.cpp): each Step reads spells until `budget` is spent and returns
    // true once everything is read; then Finish gives what CollectAllSpells
    // gives (CollectAllSpells is this with no budget, so the two are the same
    // code). Game thread, every call. Between calls it holds FormIDs and JSON,
    // never a form pointer: each step finds its spells again by FormID.
    // One step that cannot be split runs whole: the marks after the spell loop
    // (tomes, tome reach, duplicates), timed in the log (`LongestStep`).
    class FullScanCollector
    {
    public:
        explicit FullScanCollector(ScanConfig config);
        FullScanCollector(const FullScanCollector&) = delete;
        FullScanCollector& operator=(const FullScanCollector&) = delete;
        ~FullScanCollector();

        bool Step(std::chrono::microseconds budget);
        json Finish();

        [[nodiscard]] std::size_t Steps() const noexcept;
        // The longest step so far, and the phase it was in
        [[nodiscard]] double LongestStepMs() const noexcept;
        [[nodiscard]] const char* LongestStepPhase() const noexcept;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };

    // The scan fields of a Papyrus RunScan preset ("minimal", "balanced", "full";
    // anything else is "full"). "full" is what the librarian's catalog needs.
    FieldConfig FieldsForPreset(const std::string& preset);

    // Get the system instructions for LLM output format (hidden from user)
    std::string GetSystemInstructions();

    // Get spell info by FormID (for Tree Viewer)
    // Returns JSON with: formId, name, editorId, school, level, cost, type, effects, description.
    // Null when the id is malformed or names no spell.
    json GetSpellInfoJsonByFormId(const std::string& formIdStr);
    // The same, serialized; empty when not found
    std::string GetSpellInfoByFormId(const std::string& formIdStr);

    // =========================================================================
    // PERSISTENT FORMID FUNCTIONS (Load Order Resilient)
    // =========================================================================

    // Convert runtime FormID to persistent format: "PluginName.esp|0x00123456"
    // This format survives load order changes because it stores plugin name + local ID
    std::string GetPersistentFormId(RE::FormID formId);

    // Resolve persistent ID back to runtime FormID
    // Returns 0 if plugin not loaded or invalid format
    RE::FormID ResolvePersistentFormId(const std::string& persistentId);

    // Check if a FormID is currently valid (form exists in game)
    bool IsFormIdValid(RE::FormID formId);

    // Check if a FormID string is currently valid
    bool IsFormIdValid(const std::string& formIdStr);

    // Tree validation result
    struct TreeValidationResult {
        int totalNodes = 0;
        int validNodes = 0;
        int invalidNodes = 0;
        int resolvedFromPersistent = 0;               // Kept nodes whose formId changed (not the rematched ones)
        int rematchedByEditorId = 0;                  // Kept nodes found again by editor id (plugin renamed)
        bool changed = false;                         // The fixed tree differs from the file: the panel needs it
        std::vector<std::string> missingPlugins;      // Plugins persistentIds name that are not loaded
        std::vector<std::string> missingRecords;      // persistentIds of loaded plugins without that record
        std::vector<std::string> invalidFormIds;      // Saved FormIDs of the dropped nodes (their links went)
    };

    // Validate and optionally fix a spell tree JSON
    // - A node's persistentId decides its FormID; the stored formId is used for a node without one
    //   (and that node goes when a persistentId node now has its formId)
    // - Drops nodes that are not a player's spell now
    // - Updates formId fields and the links to a moved or dropped node (children,
    //   prerequisites, hardPrereqs, softPrereqs, locks[].nodeId, school root, bridges),
    //   each link looked up once by its saved formId (TreeRemap.h)
    // - Returns validation statistics
    TreeValidationResult ValidateAndFixTree(json& treeData);

    // Helper functions
    bool IsValidMagicSchool(RE::ActorValue school);
    std::string GetSchoolName(RE::ActorValue school);
    std::string GetCastingTypeName(RE::MagicSystem::CastingType type);
    std::string GetDeliveryName(RE::MagicSystem::Delivery delivery);
    std::string GetSkillLevelName(uint32_t minimumSkill);
    std::string GetSkillLevelFromPerk(RE::BGSPerk* perk);
    std::string DetermineSpellTier(RE::SpellItem* spell);
    std::string GetPluginName(RE::FormID formId);
    // The plugin of this file name when the game loaded it (full or light), nullptr when it
    // is not there or sits in Data unchecked: TESDataHandler::LookupModByName finds those too
    const RE::TESFile* LookupLoadedPlugin(std::string_view name);

    // Magic school of a spell (school of its first effect), kNone if it has none
    RE::ActorValue GetSpellSchool(RE::SpellItem* spell);

    // Stable string names for MGEF structure fields. Classification rules match
    // on these, so they must not become raw numbers.
    std::string GetArchetypeName(RE::EffectArchetype archetype);
    std::string GetActorValueName(RE::ActorValue actorValue);

    // Editor id of any form: the engine's own when it kept one, otherwise from
    // powerofthree's Tweaks (Load EditorIDs). Empty when neither has it.
    std::string GetEditorId(const RE::TESForm* form);

    // True when the keyword record is defined by the base game or an official
    // DLC (judged by its plugin, not its name). These are the only keywords the
    // traits read; the scan folds them into "traits" rather than listing them twice.
    bool IsVanillaKeyword(const RE::BGSKeyword* keyword);

    // Single source of the scan JSON shape, shared by every scan entry point.
    // Callers must have checked effect->baseEffect / spell for null.
    //
    // BuildBaseEffectJson is the MGEF half on its own, for a caller that has a
    // base effect and no spell to put it in.
    json BuildBaseEffectJson(const RE::EffectSetting* baseEffect, const FieldConfig& fields);
    json BuildEffectJson(const RE::Effect* effect, const FieldConfig& fields);
    json BuildSpellJson(RE::SpellItem* spell, RE::FormID formId, const FieldConfig& fields);

    // True when a vampire NPC carries the spell (SpellScannerCasters.cpp) -
    // the tag librarian reads it as evidence of blood magic.
    bool IsCastByVampires(RE::FormID spellFormId);
    // How this session's collection behind IsCastByVampires went (collected
    // once, at its first call): kNotRun before that. Any thread.
    ScanRecordRules::Casters VampireCastersState() noexcept;

    // What the tree goes by (SpellScannerCopies.cpp): the scan keeps every spell
    // and marks taughtByTome, voiceSlot, freeSpell, tomeUnreachable (TomeReach) and
    // duplicateOf (below); the tree takes spells a tome teaches, whose tome
    // something hands out, that are not in the voice slot, cost something to cast
    // and are not copies.
    std::unordered_set<RE::FormID> SpellsTaughtByTomes();
    // The first loaded book that teaches each spell, by the spell's FormID
    std::unordered_map<RE::FormID, RE::TESObjectBOOK*> TomesBySpell();
    bool IsEquippedInVoiceSlot(RE::SpellItem* spell);
    // A spell that costs nothing to cast (under 0.5 Magicka): a pet's or a follower's
    // call, a quest's utility spell. The scan marks it freeSpell and the tree, the
    // primed count and the Spell Research registrations leave it out, tome or not:
    // a free "spell" next to real ones breaks the picture (2026-10-05).
    bool IsFreeSpell(RE::SpellItem* spell);

    // The actors a summon brings: the NPC itself, or every NPC in its leveled
    // list and the lists inside it; an NPC whose traits (race) come from its
    // template counts as that template's NPCs (SpellScannerEvidence.cpp)
    void CollectSummonedNPCs(RE::TESForm* form, std::vector<RE::TESNPC*>& npcs, int depth = 0);

    // The spell gate (SpellScannerGate.cpp, docs/librarian/ToDo.md M9): whether a tome
    // spell is a player's spell, with the evidence. "out" leaves the tree and Spell
    // Research, "review" stays and goes to the panel's review list, "in" is the rest.
    struct GateVerdict
    {
        std::string band = "in";
        std::vector<std::string> reasons;   // free, longSummon, banishOnly, rule:<id>, horseSummon, uniqueSummon, nonHostileSummon, cheapScript
    };
    GateVerdict JudgeSpell(RE::SpellItem* spell);
    // The spells the player put back in from the panel (config.json gateReviewed), as
    // "plugin|local id" keys; read once per tree load or Spell Research pass
    std::unordered_set<std::string> ReadGateKept();
    // JudgeSpell says "out" and the player did not include the spell again
    bool GateLeavesOut(RE::SpellItem* spell, const std::unordered_set<std::string>& kept);
    // The scan's marks: freeSpell, and "gate": {band, reasons} unless the band is "in"
    void MarkGate(json& spellJson, RE::SpellItem* spell);
    // FNV-1a 64 of gate/rules.json's bytes for the scan record's fingerprint
    // (ScanRecordRules.h): "none" without the file, empty when it is there but
    // cannot be read (then no record is kept). Any thread; reads the file each call.
    std::string GateRulesFileHash();

    // Logs, by kind, the scanned spells the tree leaves out: voice slot spells,
    // spells whose tomes nothing hands out (TomeReach), and among those no tome
    // teaches NPC, trap or script copies, free spells and the rest.
    void LogSpellsOutOfTree(const std::vector<RE::SpellItem*>& spells,
        const std::unordered_set<RE::FormID>& taught, const std::unordered_set<RE::FormID>& unreachable);

    // One spell from two plugins (SpellScannerDuplicates.cpp): the same editor id
    // in the same school among the spells a tome teaches, not in the voice slot.
    // Returns each dropped record's FormID -> the kept one's (the earliest plugin's,
    // one whose tome something hands out first); the scan marks them duplicateOf.
    std::unordered_map<RE::FormID, RE::FormID> FindDuplicateSpells(const std::vector<RE::SpellItem*>& spells,
        const std::unordered_set<RE::FormID>& taught, const std::unordered_set<RE::FormID>& unreachable);

    // FindDuplicateSpells over `spells` (the same order as `spellArray`), each
    // dropped one's entry marked "duplicateOf" - the tome scan's marks.
    void MarkDuplicateSpells(json& spellArray, const std::vector<RE::SpellItem*>& spells);

    // The spell a tome of `spell` teaches through the tree: when `spell` is in a
    // duplicate group and is not a node of the tree in use, the one member that is
    // (ProgressionManager::IsTreeSpell; the kept record, or a copy an older tree
    // holds), else `spell` itself (no member a node, or two). Game thread
    // (the tree's node set has no lock). A tree node costs one set lookup; the
    // groups are worked out by PrepareTreeSpellForTome, or on the first call
    // that needs them if a tome is read before that ran.
    RE::SpellItem* TreeSpellForTome(RE::SpellItem* spell);

    // The duplicate group `spell` belongs to (the same groups TreeSpellForTome
    // reads), the kept record first; empty when it is in none. Any thread.
    std::vector<RE::FormID> DuplicateGroupOf(RE::FormID spell);

    // The tome pairs changed (SpellTomes.h): the groups are worked out again, without
    // tomeUnreachable until the new tome reach pass is done. Any thread.
    void InvalidateDuplicateGroups();

    // PrepareTreeSpellForTome on the game thread once the tome reach pass running now
    // is done; the wait is on a thread of its own (never on a cast's tome check).
    void PrepareTreeSpellForTomeAfterPass();

    // Works the groups out from the loaded spells, again once the tome reach pass
    // is done, then keeps them. Main.cpp calls it on the game thread after that
    // pass, so a cast's tome check never walks every form.
    void PrepareTreeSpellForTome();

    // Structure evidence added on top of the builders above when effectDetails
    // is on (SpellScannerEvidence.cpp): flags, projectile, explosion, hazard
    // presence, perks. Copied from the records as they are - nothing in here
    // interprets a value. Only fields checked against a real game run live
    // here; counter effects and condition presence were dropped as unverified.
    void AppendBaseEffectEvidence(json& effectJson, const RE::EffectSetting* baseEffect);
    void AppendEffectItemEvidence(json& effectJson, const RE::Effect* effect, std::size_t index);
    void AppendSpellEvidence(json& spellJson, RE::SpellItem* spell);

    // Keyword line for the spell card, as stable ids the panel translates
    // ("element.fire", "form.projectile", "school.destruction"). Derived only
    // from closed engine sets (SpellScannerChips.cpp). Not part of the scan dump.
    json BuildSpellChips(RE::SpellItem* spell);
    // The same ids without the card's length cap - what the icon rules match on.
    json BuildSpellTraits(RE::SpellItem* spell);

    // Spell card icon (SpellScannerCard.cpp). Icon packs ship SVGs named
    // KWD_<keyword>.svg for Wheeler; the key is the first keyword of the spell,
    // then of its effects, that has one. Empty when nothing is installed.
    std::string FindSpellIconKey(RE::SpellItem* spell);
    // Icon rules (card_icons.json): first rule whose traits the spell has and
    // whose file is installed. Traits are BuildSpellTraits ids.
    std::string FindRuleIconKey(RE::SpellItem* spell);
    // Wheeler's standard school emblem (icons/<school>.svg). Empty when not installed.
    std::string FindSchoolIconKey(RE::SpellItem* spell);
    std::string ReadSpellIconSvg(const std::string& key);

    // Fills <mag>/<dur>/<area> from the effect and strips <..> emphasis marks.
    std::string ResolveDescriptionTags(std::string text, const RE::Effect* effect);

    // Write a scan dump to Data/SKSE/Plugins/SpellLearning/spell_scan_output.json.
    // Returns the written path, or an empty string on failure.
    std::string WriteScanOutput(const std::string& content);

    // Whether the panel's full scan also writes that dump: on only when
    // Data/SKSE/Plugins/SpellLearning/debug.json says {"writeScanDump": true}.
    // The file ships with the debug symbols archive and the dev install, not
    // with the mod itself: the dump is 10-20 MB of analysis data.
    [[nodiscard]] bool DebugScanDumpEnabled();

    // Run a scan and write it to disk in one call, for callers outside the UI
    // (Papyrus, tests). mode is "tomes" or "all"; preset is "minimal",
    // "balanced" or "full". Returns the written path, or an empty string.
    std::string RunScanToFile(const std::string& mode, const std::string& preset);

}
