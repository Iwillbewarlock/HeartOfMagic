#include "Common.h"
#include "EncodingUtils.h"
#include "FormIdText.h"
#include "JsonText.h"
#include "SpellEffectivenessHook.h"
#include "SpellScanner.h"
#include "librarian/Librarian.h"

// =============================================================================
// SpellScannerSpellInfo - one spell's card data by formId (the tree viewer's
// GetSpellInfo and GetSpellInfoBatch), moved out of SpellScannerScan.cpp (the
// full scan) at the 600 line limit
// =============================================================================

namespace SpellScanner
{
    // =============================================================================
    // GET SPELL INFO BY FORMID (For Tree Viewer)
    // =============================================================================

    json GetSpellInfoJsonByFormId(const std::string& formIdStr)
    {
        // A hex formId ("0x00012FCC" or "00012FCC")
        const auto parsedId = FormIdText::Parse(formIdStr);
        if (!parsedId) {
            logger::error("SpellScanner: '{}' is not a formId (hex, 0x optional, at most 8 digits)", formIdStr);
            return {};
        }
        const RE::FormID formId = *parsedId;

        // Look up the spell form
        auto* form = RE::TESForm::LookupByID(formId);
        if (!form) {
            logger::warn("SpellScanner: Form not found for ID: {} (parsed: 0x{:08X})", formIdStr, formId);
            return {};
        }

        auto* spell = form->As<RE::SpellItem>();
        if (!spell) {
            logger::warn("SpellScanner: Form {} is not a spell", formIdStr);
            return {};
        }

        // Build spell info JSON
        json spellInfo;
        spellInfo["formId"] = formIdStr;
        spellInfo["name"] = EncodingUtils::SanitizeToUTF8(SpellEffectivenessHook::GetSingleton()->GetOriginalName(spell));  // own name, valid UTF-8

        const char* editorId = spell->GetFormEditorID();
        spellInfo["editorId"] = editorId ? EncodingUtils::SanitizeToUTF8(editorId) : std::string();

        // Get school and level
        std::string school = "Unknown";
        uint32_t minimumSkill = 0;

        // The scan's school and minimum skill: one effect (GetSchoolEffect)
        if (const auto* effect = GetSchoolEffect(spell); effect && effect->baseEffect) {
            school = GetSchoolName(effect->baseEffect->GetMagickSkill());
            minimumSkill = effect->baseEffect->GetMinimumSkillLevel();
        }

        // Use perk-based tier detection (fixes modded master spells with minimumSkill=0)
        std::string level = DetermineSpellTier(spell);

        spellInfo["school"] = school;
        spellInfo["level"] = level;
        spellInfo["skillLevel"] = level;  // Alias
        spellInfo["minimumSkill"] = minimumSkill;

        spellInfo["cost"] = spell->CalculateMagickaCost(nullptr);
        spellInfo["magickaCost"] = spellInfo["cost"];  // Alias

        spellInfo["type"] = GetCastingTypeName(spell->data.castingType);
        spellInfo["castingType"] = spellInfo["type"];  // Alias

        spellInfo["delivery"] = GetDeliveryName(spell->data.delivery);
        spellInfo["chargeTime"] = spell->data.chargeTime;
        spellInfo["plugin"] = GetPluginName(formId);

        // Effects
        json effectsArray = json::array();
        json effectNamesArray = json::array();
        std::string description;

        // The tree viewer only shows the plain effect fields, no MGEF structure
        const FieldConfig viewerFields{};

        // The spell's description is the first described effect's - one the
        // player is meant to see if there is any, a Hide in UI helper only as a
        // last resort. <mag>/<dur> are filled in from that same effect.
        std::string hiddenDescription;
        for (auto* effect : spell->effects) {
            if (!effect || !effect->baseEffect) continue;

            json effectJson = BuildEffectJson(effect, viewerFields);
            effectNamesArray.push_back(effectJson["name"]);

            if (effectJson.contains("description")) {
                const bool hidden = effect->baseEffect->data.flags.any(
                    RE::EffectSetting::EffectSettingData::Flag::kHideInUI);
                std::string& slot = hidden ? hiddenDescription : description;
                if (slot.empty()) {
                    slot = ResolveDescriptionTags(effectJson["description"].get<std::string>(), effect);
                }
            }
            effectsArray.push_back(effectJson);
        }
        if (description.empty()) {
            description = hiddenDescription;
        }

        spellInfo["effects"] = effectsArray;
        spellInfo["effectNames"] = effectNamesArray;
        spellInfo["description"] = description;
        // The card's keyword line, with the elements the tag librarian gives
        // (blood, water, holy ...) in place of the vanilla-keyword ones.
        json chips = BuildSpellChips(spell);
        Librarian::MergeCatalogChips(chips, GetPersistentFormId(spell->GetFormID()));
        spellInfo["chips"] = std::move(chips);

        // Name of the keyword an installed icon pack has an SVG for, if any.
        // The picture itself is fetched on demand (GetSpellIcon) - a whole tree
        // of them would not fit through one batch reply.
        // The pack author's own choice for this spell first, else the icon rules.
        // schoolIconKey is the plain emblem, safe to show while the name is hidden.
        std::string iconKey = FindSpellIconKey(spell);
        if (iconKey.empty()) {
            iconKey = FindRuleIconKey(spell);
        }
        const std::string schoolIconKey = FindSchoolIconKey(spell);
        if (!schoolIconKey.empty()) {
            spellInfo["schoolIconKey"] = schoolIconKey;
        }
        if (!iconKey.empty()) {
            spellInfo["iconKey"] = iconKey;
        }

        // Add effectiveness info for early-learned spells
        auto* effectivenessHook = SpellEffectivenessHook::GetSingleton();
        if (effectivenessHook && effectivenessHook->IsEarlyLearnedSpell(formId)) {
            float effectiveness = effectivenessHook->CalculateEffectiveness(formId);
            spellInfo["isWeakened"] = true;
            spellInfo["effectiveness"] = static_cast<int>(effectiveness * 100);  // As percentage

            // Add scaled effect values
            json scaledEffectsArray = json::array();
            for (auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) continue;

                json scaledEffect;
                scaledEffect["name"] = EncodingUtils::SanitizeToUTF8(effect->baseEffect->GetFullName());
                scaledEffect["originalMagnitude"] = effect->effectItem.magnitude;
                scaledEffect["scaledMagnitude"] = static_cast<int>(effect->effectItem.magnitude * effectiveness);
                scaledEffect["duration"] = effect->effectItem.duration;
                scaledEffectsArray.push_back(scaledEffect);
            }
            spellInfo["scaledEffects"] = scaledEffectsArray;
        } else {
            spellInfo["isWeakened"] = false;
            spellInfo["effectiveness"] = 100;
        }

        return spellInfo;
    }

    std::string GetSpellInfoByFormId(const std::string& formIdStr)
    {
        json spellInfo = GetSpellInfoJsonByFormId(formIdStr);
        return spellInfo.is_null() ? std::string() : JsonText::Dump(spellInfo);
    }
}
