#pragma once

#include "Common.h"

#include <nlohmann/json.hpp>

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
        int resolvedFromPersistent = 0;
        std::vector<std::string> missingPlugins;      // Plugins that couldn't be found
        std::vector<std::string> invalidFormIds;      // FormIDs that couldn't be resolved
    };

    // Validate and optionally fix a spell tree JSON
    // - Validates all FormIDs exist
    // - Attempts to resolve from persistentId if formId fails
    // - Updates formId field with resolved value
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

    // Run a scan and write it to disk in one call, for callers outside the UI
    // (Papyrus, tests). mode is "tomes" or "all"; preset is "minimal",
    // "balanced" or "full". Returns the written path, or an empty string.
    std::string RunScanToFile(const std::string& mode, const std::string& preset);

    // Internal scanning (returns spell array JSON, used by ScanAllSpells/ScanSpellTomes)
    json ScanSpellsToJson(const FieldConfig& fields);
}
