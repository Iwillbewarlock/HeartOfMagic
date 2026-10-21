#include "Forms.h"

#include <format>
#include <string>

namespace BlackFlame
{
    namespace
    {
        Forms g_forms;

        template <class T>
        T* Lookup(RE::TESDataHandler* dh, RE::FormID localId, const char* plugin, const char* what)
        {
            auto* form = dh->LookupForm<T>(localId, plugin);
            if (form) {
                logger::info("BlackFlame: {} = {:08X} ({}|0x{:06X})", what, form->GetFormID(), plugin, localId);
            } else {
                logger::error("BlackFlame: {} not found ({}|0x{:06X}) - is {} enabled?", what, plugin, localId, plugin);
            }
            return form;
        }

        // Quiet: the lantern's records are looked up without a line each; what is missing is named in one warning
        template <class T>
        void LookupLantern(RE::TESDataHandler* dh, RE::FormID localId, const char* type, T*& a_out, std::string& a_missing)
        {
            a_out = dh->LookupForm<T>(localId, kPluginName);
            if (!a_out) {
                a_missing += std::format("{}0x{:03X} {}", a_missing.empty() ? "" : ", ", localId, type);
            }
        }

        void LoadLanternForms(RE::TESDataHandler* dh)
        {
            auto& f = g_forms;
            std::string missing;
            LookupLantern(dh, kLanternLocalId, "ARMO", f.lantern, missing);
            LookupLantern(dh, kLanternArmaLocalId, "ARMA", f.lanternArma, missing);
            LookupLantern(dh, kLanternEnchLocalId, "ENCH", f.lanternEnch, missing);
            LookupLantern(dh, kLanternEffectLocalId, "MGEF", f.lanternEffect, missing);
            LookupLantern(dh, kLanternPerkLocalId, "PERK", f.lanternPerk, missing);
            LookupLantern(dh, kLanternBurnSpellLocalId, "SPEL", f.lanternBurnSpell, missing);
            LookupLantern(dh, kLanternBurnEffectLocalId, "MGEF", f.lanternBurnEffect, missing);
            LookupLantern(dh, kLanternSmokeLocalId, "MSTT", f.lanternSmoke, missing);
            LookupLantern(dh, kLanternPlacedLocalId, "GLOB", f.lanternPlaced, missing);
            f.lanternForms = missing.empty();
            if (f.lanternForms) {
                logger::info("BlackFlame: lantern records found ({}|0x{:03X}..0x{:03X}): lantern {:08X}, burn spell {:08X}, "
                             "effect {:08X}, smoke {:08X}, placed global {:08X}",
                    kPluginName, kLanternLocalId, kLanternPlacedLocalId, f.lantern->GetFormID(), f.lanternBurnSpell->GetFormID(),
                    f.lanternEffect->GetFormID(), f.lanternSmoke->GetFormID(),
                    f.lanternPlaced->GetFormID());
            } else {
                logger::warn("BlackFlame: lantern records missing in {} ({}) - the lantern features (placement, extra damage, "
                             "cost lines) are off; the rest of the addon is not affected", kPluginName, missing);
            }
        }
    }

    bool LoadForms()
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("BlackFlame: TESDataHandler unavailable - the addon stays off");
            return false;
        }
        g_forms.burnShader = Lookup<RE::TESEffectShader>(dh, kBurnShaderLocalId, kPluginName, "burn shader HoMBF_BurnShader");
        g_forms.stokedKeyword = Lookup<RE::BGSKeyword>(dh, kStokedKeywordLocalId, kPluginName, "keyword HoMBF_StokedKeyword");
        g_forms.stokeEffect = Lookup<RE::EffectSetting>(dh, kStokeEffectLocalId, kPluginName, "effect HoMBF_StokeEffect");
        g_forms.stokeSpell = Lookup<RE::SpellItem>(dh, kStokeSpellLocalId, kPluginName,
            "spell HoMBF_StokeSpell \"Longing\" (player.addspell this id)");
        g_forms.fireKeyword = Lookup<RE::BGSKeyword>(dh, kMagicDamageFireLocalId, kSkyrimMaster, "keyword MagicDamageFire");
        LoadLanternForms(dh);
        // Without the keyword there is no buff to detect; without the burn shader layer 1 has nothing to apply
        return g_forms.stokedKeyword && g_forms.burnShader;
    }

    const Forms& GetForms() noexcept
    {
        return g_forms;
    }
}
