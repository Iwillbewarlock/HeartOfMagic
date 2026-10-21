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
    inline constexpr RE::FormID kStokeSpellLocalId = 0x803;     // SPEL HoMBF_StokeSpell "갈망" (Longing)
    // The lantern (docs/superpowers/specs/2026-10-21-black-flame-lantern-design.md section 3)
    inline constexpr RE::FormID kLanternLocalId = 0x80D;            // ARMO HoMBF_Lantern
    inline constexpr RE::FormID kLanternArmaLocalId = 0x80E;        // ARMA HoMBF_LanternAA
    inline constexpr RE::FormID kLanternEnchLocalId = 0x80F;        // ENCH HoMBF_LanternEnch
    inline constexpr RE::FormID kLanternEffectLocalId = 0x810;      // MGEF HoMBF_LanternEffect "갈망" (Longing)
    inline constexpr RE::FormID kLanternPerkLocalId = 0x811;        // PERK HoMBF_LanternPerk (fire spell cost x1.3)
    inline constexpr RE::FormID kLanternBurnSpellLocalId = 0x812;   // SPEL HoMBF_LanternBurnSpell (the hidden extra damage)
    inline constexpr RE::FormID kLanternBurnEffectLocalId = 0x813;  // MGEF HoMBF_LanternBurnEffect
    // 0x814 was BOOK HoMBF_LuahJournal, removed (user 2026-10-21: the player's own imagination tells the story better)
    inline constexpr RE::FormID kLanternSmokeLocalId = 0x815;       // MSTT HoMBF_LanternSmoke
    inline constexpr RE::FormID kLanternPlacedLocalId = 0x816;      // GLOB HoMBF_LanternPlaced (1 once placed)
    inline constexpr const char* kSkyrimMaster = "Skyrim.esm";
    inline constexpr RE::FormID kMagicDamageFireLocalId = 0x01CEAD;  // KYWD MagicDamageFire

    struct Forms
    {
        RE::TESEffectShader* burnShader = nullptr;
        RE::BGSKeyword* stokedKeyword = nullptr;
        RE::EffectSetting* stokeEffect = nullptr;
        RE::SpellItem* stokeSpell = nullptr;
        RE::BGSKeyword* fireKeyword = nullptr;
        // The lantern's records: all of them or the lantern features are off (lanternForms false)
        bool lanternForms = false;
        RE::TESObjectARMO* lantern = nullptr;
        RE::TESObjectARMA* lanternArma = nullptr;
        RE::EnchantmentItem* lanternEnch = nullptr;
        RE::EffectSetting* lanternEffect = nullptr;
        RE::BGSPerk* lanternPerk = nullptr;
        RE::SpellItem* lanternBurnSpell = nullptr;
        RE::EffectSetting* lanternBurnEffect = nullptr;
        RE::BGSMovableStatic* lanternSmoke = nullptr;
        RE::TESGlobal* lanternPlaced = nullptr;
    };

    /// Looks every form up; false (each missing one logged) when the ESP's keyword
    /// or burn shader is missing - the addon then stays off. A missing lantern record only
    /// turns the lantern features off (one warning naming the missing ids; Forms::lanternForms).
    [[nodiscard]] bool LoadForms();

    [[nodiscard]] const Forms& GetForms() noexcept;
}
