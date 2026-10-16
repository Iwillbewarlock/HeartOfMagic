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
    // NPC_ EncDeer: unaggressive, cowardly, no crime faction - both the target and the caster
    // (hitting either is no crime; CastSpellImmediate works from any actor's caster)
    inline constexpr RE::FormID kDeerNpcLocalId = 0x0CF89D;
    inline constexpr float kDummyHealth = 100000.0f;  // the target survives every test cast

    // v2 props (Skyrim.esm, houseCARL 2026-10-16): Sparks (a non-fire spell for the launch predicate), Flames and
    // the Flame Atronach's Flames (concentration; the first whose projectile is a flamethrower in this load order),
    // Fire Rune, and the XMarker (a target for the rune)
    inline constexpr RE::FormID kSparksLocalId = 0x02DD2A;          // SPEL MAG_Sparks
    inline constexpr RE::FormID kFlamesLocalId = 0x012FCD;          // SPEL MAG_Flames
    inline constexpr RE::FormID kAtronachFlamesLocalId = 0x07E881;  // SPEL crAtronachFlameFlames
    inline constexpr RE::FormID kFireRuneLocalId = 0x05DB90;        // SPEL FireRune
    inline constexpr RE::FormID kXMarkerFormId = 0x0000003B;        // STAT XMarker

    struct V2Forms
    {
        RE::SpellItem* sparks = nullptr;
        RE::SpellItem* flames = nullptr;  // a concentration spell whose projectile is a flamethrower
        RE::SpellItem* fireRune = nullptr;
        RE::TESBoundObject* marker = nullptr;
    };

    /// The v2 props; a_detail says which were found and which Flames spell was taken
    [[nodiscard]] V2Forms LookupV2(std::string& a_detail);

    struct VanillaForms
    {
        RE::SpellItem* firebolt = nullptr;
        RE::SpellItem* fireball = nullptr;
        RE::TESNPC* deer = nullptr;
        RE::BGSExplosion* fireballExplosion = nullptr;  // from Fireball's effects (load order winner)
    };

    /// False with a_missing naming what was not found.
    [[nodiscard]] bool LookupVanilla(VanillaForms& a_out, std::string& a_missing);

    /// a_caster's instant caster: MagicCaster::CastSpellImmediate (RE/M/MagicCaster.h:62).
    /// Casts nothing and returns false without a live target.
    [[nodiscard]] bool Cast(RE::Actor* a_caster, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target);

    /// Launches a_spell's projectile from a_shooter at a_target's torso: Projectile::LaunchData
    /// (shooter, origin, angles, spell) - its spell constructor sets useOrigin and the spell
    /// (Projectile.cpp:213-222), so the impact applies the spell and the shooter is attributed -
    /// with castingSource right hand and desiredTarget set, then Projectile::Launch. Needed because
    /// CastSpellImmediate aims at the target's origin, its feet, and misses beyond ~300 units
    /// (skyrim-headless-mods headless-findings s24). Angles: the straight line, z heading =
    /// atan2(dx, dy) (as Actor::SetHeading), x pitch positive DOWN = -atan2(dz, horizontal), aimed
    /// above the target by the projectile's estimated gravity drop. No engine solve
    /// (CombatUtilities::GetAngleToProjectedTarget's AE id is unverified). Heights fall back to
    /// fixed values on a fresh spawn without bounds. No cast happens (no magicka, cast event or
    /// caster spell). Game thread only. False (nothing launched) without a shooter in a cell, spell,
    /// projectile or target; a_detail says origin, aim, drop, angles, fallbacks and the handle.
    [[nodiscard]] bool LaunchAt(RE::Actor* a_shooter, RE::SpellItem* a_spell, RE::TESObjectREFR* a_target,
        std::string& a_detail);

    /// PlaceObjectAtMe at the player, then moved a_forward units ahead and a_right
    /// units to the right of where the player faces. Null handle on failure.
    [[nodiscard]] RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_base, float a_forward, float a_right);

    /// AI off (EnableAI(false)) and health base raised to kDummyHealth; returns what was done.
    [[nodiscard]] std::string MakeTarget(RE::Actor* a_actor);

    /// The actor's current health (0 without one)
    [[nodiscard]] float HealthOf(RE::Actor* a_actor);

    /// The player's live (not dispelled or inactive) effects carrying the Stoke keyword
    [[nodiscard]] int StokeEffectCount();

    /// MagicTarget::DispelEffect on every live player effect carrying the Stoke keyword, each found again by
    /// its unique id right before; the engine queues it when the target is inside its update (C4,
    /// SelfTestDispel.cpp). Logs each effect's flags before and after; how many were dispelled or queued.
    [[nodiscard]] int DispelStoke();

    /// MagicTarget::DispelEffect on every live effect of a_spell on a_actor, as DispelStoke (SelfTestDispel.cpp); how many
    [[nodiscard]] int DispelSpell(RE::Actor* a_actor, const RE::SpellItem* a_spell);

    /// Every player effect carrying the Stoke keyword: id, flags, live or not, elapsed/duration (for FAIL lines)
    [[nodiscard]] std::string DescribeStokeEffects();

    struct ShaderOn
    {
        RE::FormID effectData = 0;
        bool finished = false;
        float age = 0.0f;  // BSTempEffect::age, seconds since it started
        std::uintptr_t ptr = 0;  // the ShaderReferenceEffect itself (identity only)
    };

    /// Every ShaderReferenceEffect whose target is a_target (collected inside the
    /// ProcessLists lock, nothing called there)
    [[nodiscard]] std::vector<ShaderOn> ShadersOn(RE::ObjectRefHandle a_target);

    /// Turns a_actor to face a_target (Actor::SetHeading, RE/A/Actor.h:682; heading z has forward
    /// (sin z, cos z)). Whether CastSpellImmediate with a target aims at it regardless of facing is
    /// not verifiable from the headers (Papyrus Spell.Cast semantics say it does): insurance.
    void FaceTarget(RE::Actor* a_actor, const RE::TESObjectREFR* a_target);

    /// Disable() and SetDelete(true); returns what was done.
    [[nodiscard]] std::string Remove(RE::ObjectRefHandle a_ref);
}
