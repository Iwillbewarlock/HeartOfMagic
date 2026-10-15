#include "Forms.h"

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
            "spell HoMBF_StokeSpell (player.addspell this id)");
        g_forms.fireKeyword = Lookup<RE::BGSKeyword>(dh, kMagicDamageFireLocalId, kSkyrimMaster, "keyword MagicDamageFire");
        // Without the keyword there is no buff to detect; without the burn shader layer 1 has nothing to apply
        return g_forms.stokedKeyword && g_forms.burnShader;
    }

    const Forms& GetForms() noexcept
    {
        return g_forms;
    }
}
