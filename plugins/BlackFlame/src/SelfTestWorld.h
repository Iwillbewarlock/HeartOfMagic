#pragma once

#include "Common.h"

#include <string>
#include <vector>

// =============================================================================
// SELF-TEST WORLD - the game actions the self-test takes (game thread only)
// =============================================================================
// Only vanilla Skyrim.esm forms and the addon's own. The FormIDs were read from
// Skyrim.esm with houseCARL (housecarl_records, 2026-10-15).

namespace BlackFlame::SelfTestWorld
{
    inline constexpr const char* kSkyrimMaster = "Skyrim.esm";
    inline constexpr RE::FormID kFireboltLocalId = 0x012FD0;   // SPEL "Firebolt" (MGEF FireDamageFFAimed)
    inline constexpr RE::FormID kFireballLocalId = 0x01C789;   // SPEL "Fireball" (MGEF FireDamageFFAimedArea)
    inline constexpr RE::FormID kDummyNpcLocalId = 0x0CF89D;   // NPC_ EncDeer: unaggressive, cowardly, no crime faction
    inline constexpr RE::FormID kCasterNpcLocalId = 0x1034E6;  // NPC_ WEFarmerFemale: unaggressive, cowardly

    struct VanillaForms
    {
        RE::SpellItem* firebolt = nullptr;
        RE::SpellItem* fireball = nullptr;
        RE::TESNPC* dummy = nullptr;
        RE::TESNPC* caster = nullptr;
    };

    /// False with a_missing naming what was not found.
    [[nodiscard]] bool LookupVanilla(VanillaForms& a_out, std::string& a_missing);

    /// a_caster's instant caster: MagicCaster::CastSpellImmediate (RE/M/MagicCaster.h:62).
    [[nodiscard]] bool Cast(RE::Actor* a_caster, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target);

    /// PlaceObjectAtMe at the player, then moved a_forward units ahead and a_right
    /// units to the right of where the player faces. Null handle on failure.
    [[nodiscard]] RE::ObjectRefHandle Spawn(RE::TESNPC* a_base, float a_forward, float a_right);

    /// AI off (EnableAI(false)) and, for the target, health raised; returns what was done.
    [[nodiscard]] std::string MakePassive(RE::Actor* a_actor, bool a_raiseHealth);

    /// ActiveEffect::Dispel(true) on every player effect carrying the Stoke keyword; how many.
    [[nodiscard]] int DispelStoke();

    struct ShaderOn
    {
        RE::FormID effectData = 0;
        bool finished = false;
    };

    /// Every ShaderReferenceEffect whose target is a_target (collected inside the
    /// ProcessLists lock, nothing called there)
    [[nodiscard]] std::vector<ShaderOn> ShadersOn(RE::ObjectRefHandle a_target);

    /// Disable() and SetDelete(true); returns what was done.
    [[nodiscard]] std::string Remove(RE::ObjectRefHandle a_ref);
}
