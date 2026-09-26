#include "Common.h"
#include "SpellScanner.h"
#include "EncodingUtils.h"
#include "librarian/Librarian.h"

#include <filesystem>
#include <unordered_set>

namespace SpellScanner
{
    namespace
    {
        // Scan dumps live next to the rest of the SpellLearning runtime data.
        constexpr const char* SCAN_OUTPUT_DIR = "Data/SKSE/Plugins/SpellLearning";
        constexpr const char* SCAN_OUTPUT_FILE = "spell_scan_output.json";
    }

    // =============================================================================
    // NAME TABLES
    // =============================================================================
    //
    // Archetype and actor value names are emitted as strings, never raw numbers:
    // the librarian's classification rules (librarian/*.json) match on them, so
    // they have to stay stable across game versions and load orders.

    std::string GetArchetypeName(RE::EffectArchetype archetype)
    {
        switch (archetype) {
            case RE::EffectArchetype::kValueModifier: return "ValueModifier";
            case RE::EffectArchetype::kScript: return "Script";
            case RE::EffectArchetype::kDispel: return "Dispel";
            case RE::EffectArchetype::kCureDisease: return "CureDisease";
            case RE::EffectArchetype::kAbsorb: return "Absorb";
            case RE::EffectArchetype::kDualValueModifier: return "DualValueModifier";
            case RE::EffectArchetype::kCalm: return "Calm";
            case RE::EffectArchetype::kDemoralize: return "Demoralize";
            case RE::EffectArchetype::kFrenzy: return "Frenzy";
            case RE::EffectArchetype::kDisarm: return "Disarm";
            case RE::EffectArchetype::kCommandSummoned: return "CommandSummoned";
            case RE::EffectArchetype::kInvisibility: return "Invisibility";
            case RE::EffectArchetype::kLight: return "Light";
            case RE::EffectArchetype::kDarkness: return "Darkness";
            case RE::EffectArchetype::kNightEye: return "NightEye";
            case RE::EffectArchetype::kLock: return "Lock";
            case RE::EffectArchetype::kOpen: return "Open";
            case RE::EffectArchetype::kBoundWeapon: return "BoundWeapon";
            case RE::EffectArchetype::kSummonCreature: return "SummonCreature";
            case RE::EffectArchetype::kDetectLife: return "DetectLife";
            case RE::EffectArchetype::kTelekinesis: return "Telekinesis";
            case RE::EffectArchetype::kParalysis: return "Paralysis";
            case RE::EffectArchetype::kReanimate: return "Reanimate";
            case RE::EffectArchetype::kSoulTrap: return "SoulTrap";
            case RE::EffectArchetype::kTurnUndead: return "TurnUndead";
            case RE::EffectArchetype::kGuide: return "Guide";
            case RE::EffectArchetype::kWerewolfFeed: return "WerewolfFeed";
            case RE::EffectArchetype::kCureParalysis: return "CureParalysis";
            case RE::EffectArchetype::kCureAddiction: return "CureAddiction";
            case RE::EffectArchetype::kCurePoison: return "CurePoison";
            case RE::EffectArchetype::kConcussion: return "Concussion";
            case RE::EffectArchetype::kValueAndParts: return "ValueAndParts";
            case RE::EffectArchetype::kAccumulateMagnitude: return "AccumulateMagnitude";
            case RE::EffectArchetype::kStagger: return "Stagger";
            case RE::EffectArchetype::kPeakValueModifier: return "PeakValueModifier";
            case RE::EffectArchetype::kCloak: return "Cloak";
            case RE::EffectArchetype::kWerewolf: return "Werewolf";
            case RE::EffectArchetype::kSlowTime: return "SlowTime";
            case RE::EffectArchetype::kRally: return "Rally";
            case RE::EffectArchetype::kEnhanceWeapon: return "EnhanceWeapon";
            case RE::EffectArchetype::kSpawnHazard: return "SpawnHazard";
            case RE::EffectArchetype::kEtherealize: return "Etherealize";
            case RE::EffectArchetype::kBanish: return "Banish";
            case RE::EffectArchetype::kSpawnScriptedRef: return "SpawnScriptedRef";
            case RE::EffectArchetype::kDisguise: return "Disguise";
            case RE::EffectArchetype::kGrabActor: return "GrabActor";
            case RE::EffectArchetype::kVampireLord: return "VampireLord";
            default: return "None";
        }
    }

    std::string GetActorValueName(RE::ActorValue actorValue)
    {
        // The engine name table is indexed by the raw enum value, so anything
        // outside [0, kTotal) would read out of bounds.
        const auto raw = std::to_underlying(actorValue);
        if (raw < 0 || raw >= std::to_underlying(RE::ActorValue::kTotal)) {
            return "None";
        }

        // Deliberately not RE::ActorValueToString: that hands back the AVIF's
        // localized display name, so a translated load order emits "체력" where an
        // English one emits "Health", and neither matches the name the rules are
        // written against ("Resist Fire" vs "FireResist"). enumName is the record's
        // language independent name, which is what the rule files can rely on.
        const auto* info = RE::ActorValueList::GetActorValueInfo(actorValue);
        if (!info || !info->enumName) {
            return "None";
        }
        return info->enumName;
    }

    // =============================================================================
    // EDITOR IDS
    // =============================================================================
    //
    // The engine throws most editor ids away after loading - keywords keep
    // theirs, spells and magic effects do not, so GetFormEditorID() on a spell
    // is always empty. powerofthree's Tweaks ("Load EditorIDs") keeps them all
    // and hands them out through an exported function; nearly every modded setup
    // has it. Editor ids matter here because they are English on every load
    // order, whatever language the names were translated into.

    std::string GetEditorId(const RE::TESForm* form)
    {
        if (!form) return "";

        const char* native = form->GetFormEditorID();
        if (native && native[0] != '\0') return native;

        using GetFormEditorID_t = const char* (*)(std::uint32_t);
        static const GetFormEditorID_t tweaksLookup = []() -> GetFormEditorID_t {
            const HMODULE tweaks = GetModuleHandleW(L"po3_Tweaks");
            if (!tweaks) return nullptr;
            return reinterpret_cast<GetFormEditorID_t>(GetProcAddress(tweaks, "GetFormEditorID"));
        }();

        if (!tweaksLookup) return "";
        const char* cached = tweaksLookup(form->GetFormID());
        return cached ? cached : "";
    }

    // =============================================================================
    // VANILLA KEYWORDS
    // =============================================================================
    //
    // "Vanilla" is decided by where the keyword record lives, not by what it is
    // called: a keyword defined in the base game or an official DLC. A mod that
    // names its own keyword MagicSomething does not pass.

    bool IsVanillaKeyword(const RE::BGSKeyword* keyword)
    {
        static const std::unordered_set<std::string> kOfficialPlugins = {
            "skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"
        };

        if (!keyword) return false;
        std::string plugin = GetPluginName(keyword->GetFormID());
        std::transform(plugin.begin(), plugin.end(), plugin.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return kOfficialPlugins.contains(plugin);
    }

    // =============================================================================
    // SPELL / EFFECT JSON
    // =============================================================================

    RE::ActorValue GetSpellSchool(RE::SpellItem* spell)
    {
        if (!spell || spell->effects.empty()) {
            return RE::ActorValue::kNone;
        }

        auto* firstEffect = spell->effects[0];
        if (!firstEffect || !firstEffect->baseEffect) {
            return RE::ActorValue::kNone;
        }

        return firstEffect->baseEffect->GetMagickSkill();
    }

    // Everything that comes from the MGEF record itself, split from what a
    // particular spell asks of it (magnitude, duration, area).
    json BuildBaseEffectJson(const RE::EffectSetting* baseEffect, const FieldConfig& fields)
    {
        json effectJson;

        effectJson["name"] = EncodingUtils::SanitizeToUTF8(baseEffect->GetFullName());

        // English on every load order, unlike the name (empty without po3 Tweaks)
        const std::string effectEditorId = GetEditorId(baseEffect);
        if (!effectEditorId.empty()) {
            effectJson["editorId"] = effectEditorId;
        }

        const char* description = baseEffect->magicItemDescription.c_str();
        if (description && strlen(description) > 0) {
            effectJson["description"] = EncodingUtils::SanitizeToUTF8(description);
        }

        if (!fields.effectDetails) {
            return effectJson;
        }

        // MGEF structure - the language independent evidence the librarian
        // classifies on. Vanilla Magic* keywords live here, not on the SPEL.
        json keywordsArray = json::array();
        for (auto* keyword : baseEffect->GetKeywords()) {
            if (!keyword) continue;
            const char* keywordEditorId = keyword->GetFormEditorID();
            if (keywordEditorId && strlen(keywordEditorId) > 0) {
                keywordsArray.push_back(keywordEditorId);
            }
        }
        effectJson["keywords"] = keywordsArray;

        effectJson["archetype"] = GetArchetypeName(baseEffect->data.archetype);
        effectJson["primaryAV"] = GetActorValueName(baseEffect->data.primaryAV);
        effectJson["secondaryAV"] = GetActorValueName(baseEffect->data.secondaryAV);
        effectJson["resistance"] = GetActorValueName(baseEffect->data.resistVariable);
        effectJson["hostile"] = baseEffect->IsHostile();
        effectJson["detrimental"] = baseEffect->IsDetrimental();
        effectJson["castingType"] = GetCastingTypeName(baseEffect->data.castingType);
        effectJson["delivery"] = GetDeliveryName(baseEffect->data.delivery);
        effectJson["magicSkill"] = GetSchoolName(baseEffect->GetMagickSkill());

        // Summons, bound weapons and scripted refs point at the form they spawn.
        if (baseEffect->data.associatedForm) {
            effectJson["associatedForm"] = GetPersistentFormId(baseEffect->data.associatedForm->GetFormID());
        }

        AppendBaseEffectEvidence(effectJson, baseEffect);

        return effectJson;
    }

    json BuildEffectJson(const RE::Effect* effect, const FieldConfig& fields)
    {
        // The MGEF half, plus what this particular spell asks of it.
        json effectJson = BuildBaseEffectJson(effect->baseEffect, fields);

        effectJson["magnitude"] = effect->effectItem.magnitude;
        effectJson["duration"] = effect->effectItem.duration;
        effectJson["area"] = effect->effectItem.area;

        return effectJson;
    }

    json BuildSpellJson(RE::SpellItem* spell, RE::FormID formId, const FieldConfig& fields)
    {
        json spellJson;

        // Essential fields (always included)
        spellJson["formId"] = std::format("0x{:08X}", formId);
        spellJson["persistentId"] = GetPersistentFormId(formId);  // Load order resilient ID
        spellJson["name"] = EncodingUtils::SanitizeToUTF8(spell->GetFullName());  // Sanitize for valid UTF-8 JSON
        spellJson["school"] = GetSchoolName(GetSpellSchool(spell));
        spellJson["skillLevel"] = DetermineSpellTier(spell);
        if (IsCastByVampires(formId)) {
            spellJson["castByVampires"] = true;
        }

        // Optional fields
        if (fields.editorId) {
            // Empty string when not available (no po3 Tweaks)
            spellJson["editorId"] = GetEditorId(spell);
        }
        if (fields.magickaCost) {
            spellJson["magickaCost"] = spell->CalculateMagickaCost(nullptr);
        }
        if (fields.minimumSkill) {
            uint32_t minSkill = 0;
            if (spell->effects.size() > 0 && spell->effects[0] && spell->effects[0]->baseEffect) {
                minSkill = spell->effects[0]->baseEffect->GetMinimumSkillLevel();
            }
            spellJson["minimumSkill"] = minSkill;
        }
        if (fields.castingType) {
            spellJson["castingType"] = GetCastingTypeName(spell->data.castingType);
        }
        if (fields.delivery) {
            spellJson["delivery"] = GetDeliveryName(spell->data.delivery);
        }
        if (fields.chargeTime) {
            spellJson["chargeTime"] = spell->data.chargeTime;
        }
        if (fields.plugin) {
            spellJson["plugin"] = GetPluginName(formId);
        }

        // Effects
        if (fields.effects) {
            json effectsArray = json::array();
            for (std::size_t index = 0; index < spell->effects.size(); index++) {
                const auto* effect = spell->effects[static_cast<std::uint32_t>(index)];
                if (!effect || !effect->baseEffect) continue;

                json effectJson = BuildEffectJson(effect, fields);
                if (fields.effectDetails) {
                    // index is the slot in the record, so a skipped broken
                    // effect leaves a gap instead of renumbering the rest.
                    AppendEffectItemEvidence(effectJson, effect, index);
                }
                effectsArray.push_back(effectJson);
            }
            spellJson["effects"] = effectsArray;
        } else if (fields.effectNames) {
            json effectNamesArray = json::array();
            for (auto* effect : spell->effects) {
                if (effect && effect->baseEffect) {
                    effectNamesArray.push_back(EncodingUtils::SanitizeToUTF8(effect->baseEffect->GetFullName()));
                }
            }
            spellJson["effectNames"] = effectNamesArray;
        }

        // Keywords (SPEL level - framework tags like KIT_/OCF_ live here)
        if (fields.keywords && spell->keywords) {
            json keywordsArray = json::array();
            for (uint32_t i = 0; i < spell->numKeywords; i++) {
                if (spell->keywords[i]) {
                    const char* kwEditorId = spell->keywords[i]->GetFormEditorID();
                    if (kwEditorId && strlen(kwEditorId) > 0) {
                        keywordsArray.push_back(kwEditorId);
                    }
                }
            }
            spellJson["keywords"] = keywordsArray;
        }

        if (fields.effectDetails) {
            AppendSpellEvidence(spellJson, spell);

            // The one normalised column. "keywords" above stays the raw names as
            // the plugins wrote them; traits is what those and the engine values
            // boil down to in a fixed vocabulary (element.fire, kind.summon ...),
            // with the base game's own keywords folded in - MagicSummonFire and a
            // fire resist value both come out as element.fire. Derived, not
            // copied, which is why it has its own name.
            spellJson["traits"] = BuildSpellTraits(spell);
        }

        return spellJson;
    }

    // =============================================================================
    // SCAN OUTPUT FILE
    // =============================================================================

    std::string WriteScanOutput(const std::string& content)
    {
        try {
            std::filesystem::path outputDir(SCAN_OUTPUT_DIR);
            std::filesystem::create_directories(outputDir);

            std::filesystem::path outputPath = outputDir / SCAN_OUTPUT_FILE;
            std::ofstream file(outputPath);
            if (!file.is_open()) {
                logger::error("SpellScanner: Failed to open {} for writing", outputPath.string());
                return "";
            }

            file << content;
            file.close();

            logger::info("SpellScanner: Wrote scan output to {} ({} bytes)", outputPath.string(), content.size());
            return outputPath.string();
        } catch (const std::exception& e) {
            logger::error("SpellScanner: Exception while writing scan output: {}", e.what());
            return "";
        }
    }

    // Mirrors the presets in PrismaUI modules/llmApiSettings.js applyPreset().
    // Keep the two in step: the UI and Papyrus must produce the same dump.
    static FieldConfig FieldsForPreset(const std::string& preset)
    {
        FieldConfig fields;

        if (preset == "minimal") {
            fields.magickaCost = false;
            fields.effectNames = true;
        } else if (preset == "balanced") {
            fields.effectNames = true;
        } else {
            // "full" - everything the librarian needs, including MGEF structure
            fields.minimumSkill = true;
            fields.castingType = true;
            fields.delivery = true;
            fields.chargeTime = true;
            fields.plugin = true;
            fields.effects = true;
            fields.keywords = true;
            fields.effectDetails = true;
        }

        return fields;
    }

    std::string RunScanToFile(const std::string& mode, const std::string& preset)
    {
        const std::string effectivePreset = preset.empty() ? "full" : preset;
        if (effectivePreset != "minimal" && effectivePreset != "balanced" && effectivePreset != "full") {
            logger::warn("SpellScanner: Unknown scan preset '{}', using 'full'", effectivePreset);
        }

        ScanConfig config;
        config.fields = FieldsForPreset(effectivePreset);

        if (!mode.empty() && mode != "tomes" && mode != "all") {
            logger::warn("SpellScanner: Unknown scan mode '{}', using 'tomes'", mode);
        }
        const bool tomeMode = (mode != "all");

        logger::info("SpellScanner: RunScanToFile mode='{}' preset='{}'",
            tomeMode ? "tomes" : "all", effectivePreset);

        std::string result = tomeMode ? ScanSpellTomes(config) : ScanAllSpells(config);

        // The librarian classifies whatever the scan just produced and merges
        // its elements into the traits, before the dump is written so the file
        // carries them. It reports its own failures; the scan's own result
        // stands either way.
        Librarian::ClassifyScan(result);

        return WriteScanOutput(result);
    }
}
