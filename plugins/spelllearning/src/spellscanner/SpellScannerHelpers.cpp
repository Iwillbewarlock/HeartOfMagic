#include "Common.h"
#include "SpellScanner.h"
#include "EncodingUtils.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace SpellScanner
{
    // =============================================================================
    // SYSTEM INSTRUCTIONS (Hidden from user - defines output format)
    // =============================================================================

    std::string GetSystemInstructions()
    {
        return R"(
## OUTPUT FORMAT REQUIREMENTS (CRITICAL - Follow exactly)

You MUST return ONLY valid JSON matching this exact schema. No explanations, no markdown code blocks, just raw JSON.

{
  "version": "1.0",
  "schools": {
    "Alteration": {
      "root": "0xFORMID_OF_ROOT_SPELL",
      "nodes": [
        {
          "formId": "0xFORMID",
          "children": ["0xCHILD_FORMID_1", "0xCHILD_FORMID_2"],
          "prerequisites": ["0xPREREQ_FORMID"],
          "tier": 1
        }
      ]
    },
    "Conjuration": { ... },
    "Destruction": { ... },
    "Illusion": { ... },
    "Restoration": { ... }
  }
}

### Field Requirements:
- **formId**: The hex FormID from the spell data (e.g., "0x00012FCD"). MUST match exactly.
- **children**: Array of formIds that this spell unlocks. Empty array [] if none.
- **prerequisites**: Array of formIds required before learning. Empty array [] for root spells.
- **tier**: Integer depth in tree. Root = 1, children of root = 2, etc.
- **root**: The formId of the single root spell for each school.

### Critical Rules:
1. Use ONLY formIds in the output - names/descriptions are NOT needed (retrieved in-game)
2. Every spell from the input MUST appear exactly once in the output
3. Each school has exactly ONE root spell (prerequisites = [])
4. FormIds must be EXACT matches from the spell data - no modifications
5. Return raw JSON only - no markdown, no explanations, no code fences

## SPELL DATA:
)";
    }

    // =============================================================================
    // CONFIG PARSING
    // =============================================================================

    ScanConfig ParseScanConfig(const std::string& jsonConfig)
    {
        ScanConfig config;

        if (jsonConfig.empty()) {
            return config;
        }

        try {
            json j = json::parse(jsonConfig);

            // Parse fields object
            if (j.contains("fields")) {
                auto& f = j["fields"];
                if (f.contains("editorId")) config.fields.editorId = f["editorId"].get<bool>();
                if (f.contains("magickaCost")) config.fields.magickaCost = f["magickaCost"].get<bool>();
                if (f.contains("minimumSkill")) config.fields.minimumSkill = f["minimumSkill"].get<bool>();
                if (f.contains("castingType")) config.fields.castingType = f["castingType"].get<bool>();
                if (f.contains("delivery")) config.fields.delivery = f["delivery"].get<bool>();
                if (f.contains("chargeTime")) config.fields.chargeTime = f["chargeTime"].get<bool>();
                if (f.contains("plugin")) config.fields.plugin = f["plugin"].get<bool>();
                if (f.contains("effects")) config.fields.effects = f["effects"].get<bool>();
                if (f.contains("effectNames")) config.fields.effectNames = f["effectNames"].get<bool>();
                if (f.contains("keywords")) config.fields.keywords = f["keywords"].get<bool>();
                if (f.contains("effectDetails")) config.fields.effectDetails = f["effectDetails"].get<bool>();
            }

            // Parse tree rules prompt
            if (j.contains("treeRulesPrompt")) {
                config.treeRulesPrompt = j["treeRulesPrompt"].get<std::string>();
            }

            logger::info("SpellScanner: ScanConfig parsed - editorId:{}, treeRulesPrompt length:{}",
                config.fields.editorId, config.treeRulesPrompt.length());
        } catch (const std::exception& e) {
            logger::warn("SpellScanner: Failed to parse scan config: {}", e.what());
        }

        return config;
    }

    FieldConfig ParseFieldConfig(const std::string& jsonConfig)
    {
        FieldConfig config;

        if (jsonConfig.empty()) {
            return config;
        }

        try {
            json j = json::parse(jsonConfig);

            if (j.contains("editorId")) config.editorId = j["editorId"].get<bool>();
            if (j.contains("magickaCost")) config.magickaCost = j["magickaCost"].get<bool>();
            if (j.contains("minimumSkill")) config.minimumSkill = j["minimumSkill"].get<bool>();
            if (j.contains("castingType")) config.castingType = j["castingType"].get<bool>();
            if (j.contains("delivery")) config.delivery = j["delivery"].get<bool>();
            if (j.contains("chargeTime")) config.chargeTime = j["chargeTime"].get<bool>();
            if (j.contains("plugin")) config.plugin = j["plugin"].get<bool>();
            if (j.contains("effects")) config.effects = j["effects"].get<bool>();
            if (j.contains("effectNames")) config.effectNames = j["effectNames"].get<bool>();
            if (j.contains("keywords")) config.keywords = j["keywords"].get<bool>();
            if (j.contains("effectDetails")) config.effectDetails = j["effectDetails"].get<bool>();

            logger::info("SpellScanner: FieldConfig parsed - editorId:{}, magickaCost:{}",
                config.editorId, config.magickaCost);
        } catch (const std::exception& e) {
            logger::warn("SpellScanner: Failed to parse field config: {}", e.what());
        }

        return config;
    }

    // =============================================================================
    // HELPER FUNCTIONS
    // =============================================================================

    bool IsValidMagicSchool(RE::ActorValue school)
    {
        switch (school) {
            case RE::ActorValue::kAlteration:
            case RE::ActorValue::kConjuration:
            case RE::ActorValue::kDestruction:
            case RE::ActorValue::kIllusion:
            case RE::ActorValue::kRestoration:
                return true;
            default:
                return false;
        }
    }

    std::string GetSchoolName(RE::ActorValue school)
    {
        switch (school) {
            case RE::ActorValue::kAlteration: return "Alteration";
            case RE::ActorValue::kConjuration: return "Conjuration";
            case RE::ActorValue::kDestruction: return "Destruction";
            case RE::ActorValue::kIllusion: return "Illusion";
            case RE::ActorValue::kRestoration: return "Restoration";
            default: return "Unknown";
        }
    }

    std::string GetCastingTypeName(RE::MagicSystem::CastingType type)
    {
        switch (type) {
            case RE::MagicSystem::CastingType::kConstantEffect: return "Constant Effect";
            case RE::MagicSystem::CastingType::kFireAndForget: return "Fire and Forget";
            case RE::MagicSystem::CastingType::kConcentration: return "Concentration";
            case RE::MagicSystem::CastingType::kScroll: return "Scroll";
            default: return "Unknown";
        }
    }

    std::string GetDeliveryName(RE::MagicSystem::Delivery delivery)
    {
        switch (delivery) {
            case RE::MagicSystem::Delivery::kSelf: return "Self";
            case RE::MagicSystem::Delivery::kTouch: return "Touch";
            case RE::MagicSystem::Delivery::kAimed: return "Aimed";
            case RE::MagicSystem::Delivery::kTargetActor: return "Target Actor";
            case RE::MagicSystem::Delivery::kTargetLocation: return "Target Location";
            default: return "Unknown";
        }
    }

    std::string GetSkillLevelName(uint32_t minimumSkill)
    {
        if (minimumSkill < 25) return "Novice";
        if (minimumSkill < 50) return "Apprentice";
        if (minimumSkill < 75) return "Adept";
        if (minimumSkill < 100) return "Expert";
        return "Master";
    }

    // =============================================================================
    // HALF-COST PERK TIERS
    // =============================================================================
    //
    // A spell's half-cost perk says its tier outright, but the engine keeps no
    // editor id for perks: GetFormEditorID() is empty for every BGSPerk, so the
    // name patterns below never saw one and every spell fell back to its first
    // effect's minimum skill (a Master spell whose effect says 0 became Novice).
    // The 25 vanilla perks are known by FormID instead. Skyrim.esm is always load
    // index 0x00, so their runtime FormIDs are these values. Perk overhauls edit
    // these records in place, so the FormIDs hold with Adamant, Ordinator and the
    // rest; a spell with a mod's own half-cost perk falls through to the name.

    namespace
    {
        struct HalfCostPerkTier
        {
            RE::FormID formId;
            const char* tier;
        };

        // AlterationNovice00, AlterationApprentice25 ... RestorationMaster100
        constexpr HalfCostPerkTier kVanillaHalfCostPerks[] = {
            { 0x000F2CA6, "Novice" }, { 0x000C44B7, "Apprentice" }, { 0x000C44B8, "Adept" },
            { 0x000C44B9, "Expert" }, { 0x000C44BA, "Master" },                                 // Alteration
            { 0x000F2CA7, "Novice" }, { 0x000C44BB, "Apprentice" }, { 0x000C44BC, "Adept" },
            { 0x000C44BD, "Expert" }, { 0x000C44BE, "Master" },                                 // Conjuration
            { 0x000F2CA8, "Novice" }, { 0x000C44BF, "Apprentice" }, { 0x000C44C0, "Adept" },
            { 0x000C44C1, "Expert" }, { 0x000C44C2, "Master" },                                 // Destruction
            { 0x000F2CA9, "Novice" }, { 0x000C44C3, "Apprentice" }, { 0x000C44C4, "Adept" },
            { 0x000C44C5, "Expert" }, { 0x000C44C6, "Master" },                                 // Illusion
            { 0x000F2CAA, "Novice" }, { 0x000C44C7, "Apprentice" }, { 0x000C44C8, "Adept" },
            { 0x000C44C9, "Expert" }, { 0x000C44CA, "Master" },                                 // Restoration
        };

        const char* VanillaHalfCostPerkTier(RE::FormID formId)
        {
            for (const auto& entry : kVanillaHalfCostPerks) {
                if (entry.formId == formId) return entry.tier;
            }
            return nullptr;
        }
    }

    std::string GetSkillLevelFromPerk(RE::BGSPerk* perk)
    {
        if (!perk) return "";

        if (const char* vanillaTier = VanillaHalfCostPerkTier(perk->GetFormID())) {
            return vanillaTier;
        }

        // A mod's own perk: its name, when po3 Tweaks keeps editor ids
        const std::string id = GetEditorId(perk);
        if (id.empty()) return "";

        std::string lower = id;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // The vanilla pattern is {School}{Tier}{Number}, e.g. DestructionMaster100,
        // so only the END of the name counts: a tier word followed by nothing but
        // digits. A word elsewhere in the name ("SpellmasterAdeptness") says
        // nothing about the tier.
        const std::size_t digitsStart = lower.find_last_not_of("0123456789") + 1;
        const std::string_view stem = std::string_view(lower).substr(0, digitsStart);
        const std::string_view digits = std::string_view(lower).substr(digitsStart);

        static constexpr std::pair<std::string_view, const char*> kTierWords[] = {
            { "master", "Master" }, { "expert", "Expert" }, { "adept", "Adept" },
            { "apprentice", "Apprentice" }, { "novice", "Novice" },
        };
        for (const auto& [word, tier] : kTierWords) {
            if (stem.ends_with(word)) return tier;
        }

        // No tier word: the vanilla skill numbers, but only straight after a
        // school name ("Destruction75"), not any id ending in 00
        static constexpr std::string_view kSchoolWords[] = {
            "alteration", "conjuration", "destruction", "illusion", "restoration",
        };
        static constexpr std::pair<std::string_view, const char*> kTierNumbers[] = {
            { "00", "Novice" }, { "25", "Apprentice" }, { "50", "Adept" },
            { "75", "Expert" }, { "100", "Master" },
        };
        const bool afterSchool = std::ranges::any_of(kSchoolWords,
            [&](std::string_view school) { return stem.ends_with(school); });
        if (afterSchool) {
            for (const auto& [number, tier] : kTierNumbers) {
                if (digits == number) return tier;
            }
        }

        return "";  // Unknown perk, caller should fall back to minimumSkill
    }

    std::string DetermineSpellTier(RE::SpellItem* spell)
    {
        if (!spell) return "Novice";

        // First: the half-cost perk (vanilla perks by FormID, a mod's own by name)
        // CommonLib calls this castingPerk, but it's the HalfCostPerk field in the SPEL record
        if (spell->data.castingPerk) {
            std::string perkTier = GetSkillLevelFromPerk(spell->data.castingPerk);
            if (!perkTier.empty()) {
                return perkTier;
            }
        }

        // Fallback: use minimumSkill from first effect
        uint32_t minimumSkill = 0;
        if (spell->effects.size() > 0) {
            auto* firstEffect = spell->effects[0];
            if (firstEffect && firstEffect->baseEffect) {
                minimumSkill = firstEffect->baseEffect->GetMinimumSkillLevel();
            }
        }

        return GetSkillLevelName(minimumSkill);
    }

    // In UTF-8, like the plugin part of GetPersistentFormId: the two are compared
    std::string GetPluginName(RE::FormID formId)
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return "Unknown";

        uint8_t modIndex = (formId >> 24) & 0xFF;

        if (modIndex == 0xFE) {
            uint16_t lightIndex = (formId >> 12) & 0xFFF;
            const auto* file = dataHandler->LookupLoadedLightModByIndex(lightIndex);
            if (file) {
                return EncodingUtils::SanitizeToUTF8(file->fileName);
            }
        } else {
            const auto* file = dataHandler->LookupLoadedModByIndex(modIndex);
            if (file) {
                return EncodingUtils::SanitizeToUTF8(file->fileName);
            }
        }

        return "Unknown";
    }
}
