#pragma once

#include "Common.h"

#include <string>
#include <vector>

// =============================================================================
// FIRE CLOSURE (v2 expansion C1) - every form the player's fire magic can show
// =============================================================================
// Built at kDataLoaded from the ORIGINAL forms (the copies' links are not followed), after
// the fire sets. Seeds, for every fire spell (FireSet, cloak-associated spells included):
//   (a) its fire MGEFs (IsBurnable);
//   (b) its costliest effect's MGEF (MagicItem::GetAVEffect, the one the engine takes the
//       projectile and the casting art from), fire or not.
// And for every bound fire weapon seed (FireSet.h BoundFireSeeds, bound-weapon stage 1; edge kEdgeBoundArrow, via = the
// seed's MGEF): its MGEF's projectileBase (bound bow MGEFs carry the arrow's PROJ there), and for a bow or crossbow the
// PROJ of every AMMO with the WeapTypeBoundArrow default-object keyword (the arrows a bound-bow script hands out; an
// over-approximation, harmless: the Launch swap needs the player's bound fire weapon as the weapon source).
// Edges, breadth first to a fixed point:
//   MGEF  projectileBase, explosion, impactDataSet, castingArt, dualCastData, a spawn-hazard
//         archetype's associated form; for a fire MGEF also hitEffectArt and hitVisuals' art
//         (RFCT data.artObject - the engine prefers it)
//   DUAL  pProjectile, pExplosion, pImpactDataSet, pHitEffectArt
//   PROJ  explosionType          EXPL  impactDataSet, impactPlacedObject, spawnProjectile
//   IPDS  every impact            IPCT  hazard
//   HAZD  impactDataSet, and its spell's MGEFs (as seeds, without making the spell fire)
// Copy types: PROJ, EXPL, IPCT, HAZD, ARTO. IPDS, DUAL, RFCT, SPEL and MGEF are passed through.
// Excluded (with the reason): a form defined in one of our own plugins (HeartOfMagic_BlackFlame*),
// a placed object that is not a copy type, an art with alternate textures (MODS: not copied).

namespace BlackFlame
{
    enum class CopyKind
    {
        kProjectile,
        kExplosion,
        kImpact,
        kHazard,
        kArt
    };

    /// "PROJ" / "EXPL" / "IPCT" / "HAZD" / "ARTO"
    [[nodiscard]] const char* CopyKindTag(CopyKind a_kind) noexcept;

    struct FireClosure;

    // C5 (T0-cover): the edges that reached a form, so the self-test can tell a copy that no swap point uses
    enum ClosureEdge : std::uint32_t
    {
        kEdgeOther = 1u << 0,
        kEdgeCastingArt = 1u << 1,           // MGEF.castingArt of an effect not reached from a costliest seed
        kEdgeCastingArtCostliest = 1u << 2,  // MGEF.castingArt from a costliest-effect seed (SetCastingArt shows it)
        kEdgeHitArt = 1u << 3,               // a fire MGEF's hit art, RFCT art or DUAL hit art (the 0x0C swap)
        kEdgeSpawnHazard = 1u << 4,          // a spawn-hazard MGEF that is not a fire effect (no swap point)
        kEdgeSpawnHazardFire = 1u << 5,      // a fire spawn-hazard MGEF (the spawn window)
        kEdgeBoundArrow = 1u << 6            // a bound fire weapon's arrow PROJ (the Launch swap with the weapon as source)
    };

    struct ClosureForm
    {
        RE::TESForm* form = nullptr;
        CopyKind kind = CopyKind::kProjectile;
        RE::FormID viaSpell = 0;          // the first spell whose walk reached it (a bound fire weapon seed's MGEF for its arrows)
        bool reachedByCostliest = false;  // some walk from a costliest-effect seed reached it
        std::uint32_t edges = 0;          // ClosureEdge bits of every edge that reached it
    };

    /// The closure the last BuildFireClosure made (kept for the self-test; kDataLoaded writes it once)
    [[nodiscard]] const FireClosure& LastFireClosure() noexcept;

    struct ClosureExcluded
    {
        RE::TESForm* form = nullptr;
        std::string reason;
        RE::FormID viaSpell = 0;
    };

    struct FireClosure
    {
        std::vector<ClosureForm> forms;  // copy types, in the order first reached
        std::vector<ClosureExcluded> excluded;
        std::size_t spells = 0;          // seed spells
        std::size_t passedThrough = 0;   // MGEF, SPEL, IPDS, DUAL and RFCT forms walked
        std::size_t boundSeeds = 0;      // bound fire weapon seeds
        std::size_t boundArrowAmmo = 0;  // AMMO with the WeapTypeBoundArrow keyword (their PROJs pushed for bows)
    };

    /// kDataLoaded thread, after BuildFireSets (also kept as LastFireClosure)
    [[nodiscard]] FireClosure BuildFireClosure();

    /// The form's own model path as its record stores it (TESModel::model), or empty
    [[nodiscard]] std::string ModelPathOf(const RE::TESForm* a_form);

    /// The form's defining file is one of our own plugins (HeartOfMagic_BlackFlame*, case ignored)
    [[nodiscard]] bool IsOwnPluginForm(const RE::TESForm* a_form);

    /// "Plugin.esp|00XXXX": the defining file's name (UTF-8) and the local id (12 bits in a light plugin)
    [[nodiscard]] std::string SourceText(const RE::TESForm* a_form);
}
