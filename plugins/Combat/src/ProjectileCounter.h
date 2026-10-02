#pragma once

// =============================================================================
// ProjectileCounter - counters spell missiles at the moment they hit the player
// =============================================================================
// A counter raises the player's strongest ward and lets the hit go on, so the
// ward absorbs it (counterResult "ward"). Fallbacks: "absorb" removes the
// missile, "reflect" sends it back at its caster (kept for later).
// Hooks MissileProjectile vtable slot 0xBE (190). CommonLib labels that slot
// HandleHits(hkpCollidable*) -> bool, but Elden Parry (2022) and its GPL
// remake both hook it as (Projectile*, hkpAllCdPointCollector*), and the
// collector's hit pairs are what we read. The hook returns bool: the original
// result on the normal path, CombatConfig::hookReturnOnCounter on a counter.
// Beams, cones and flames are other classes and are not countered (S3).
// =============================================================================

namespace SLCombat
{
    class ProjectileCounter
    {
    public:
        static void Install();

        /// Reflect a projectile back at its shooter, owned by a_newOwner.
        static void Reflect(RE::Projectile* a_projectile, RE::Actor* a_newOwner,
            RE::hkpCollidable* a_projectileCollidable, RE::TESObjectREFR* a_target);

    private:
        static bool OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_collector);

        static inline REL::Relocation<decltype(OnMissileCollision)> s_original;
    };

    /// The collidable on the other side of a hit pair whose one side is a_actor,
    /// or null when a_actor is in no pair.
    RE::hkpCollidable* OtherSideOfHit(RE::hkpAllCdPointCollector* a_collector, RE::Actor* a_actor);

    /// Logs the projectile-parry DLLs that are loaded. Returns true when one of
    /// them reflects spells from a ward held in hand (Perfectly Valid Wards), so
    /// ours stands down when a hand holds a ward. Weapon/shield parry mods (Elden
    /// Parry, Parry for All, Dual Wield Parrying) parry on a block; while our
    /// prompt shows SkyPrompt takes the block key, so they do not count.
    bool DetectWardReflectMod();
}
