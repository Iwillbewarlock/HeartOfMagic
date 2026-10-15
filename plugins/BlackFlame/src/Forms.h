#pragma once

#include "Common.h"

// =============================================================================
// FORMS - the addon ESP's records and the vanilla fire keyword
// =============================================================================
// Looked up once on the game thread at kDataLoaded; read-only afterwards.

namespace BlackFlame
{
    inline constexpr const char* kPluginName = "HeartOfMagic_BlackFlame.esp";
    inline constexpr RE::FormID kBurnShaderLocalId = 0x800;     // EFSH HoMBF_BurnShader
    inline constexpr RE::FormID kStokedKeywordLocalId = 0x801;  // KYWD HoMBF_StokedKeyword
    inline constexpr RE::FormID kStokeEffectLocalId = 0x802;    // MGEF HoMBF_StokeEffect
    inline constexpr RE::FormID kStokeSpellLocalId = 0x803;     // SPEL HoMBF_StokeSpell "흑염 달구기"
    inline constexpr const char* kSkyrimMaster = "Skyrim.esm";
    inline constexpr RE::FormID kMagicDamageFireLocalId = 0x01CEAD;  // KYWD MagicDamageFire

    struct Forms
    {
        RE::TESEffectShader* burnShader = nullptr;
        RE::BGSKeyword* stokedKeyword = nullptr;
        RE::EffectSetting* stokeEffect = nullptr;
        RE::SpellItem* stokeSpell = nullptr;
        RE::BGSKeyword* fireKeyword = nullptr;
    };

    /// Looks every form up; false (each missing one logged) when the ESP's keyword
    /// or burn shader is missing - the addon then stays off.
    [[nodiscard]] bool LoadForms();

    [[nodiscard]] const Forms& GetForms() noexcept;
}
