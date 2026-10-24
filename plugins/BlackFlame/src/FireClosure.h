#pragma once

#include "Common.h"

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// FIRE CLOSURE (private visual copies) - every form eligible magic can show
// =============================================================================
// Built at kDataLoaded from the ORIGINAL forms (the copies' links are not followed), after
// the gameplay fire and visual sets. Seeds, for every visual spell (VisualSet, catalog fire roots and their children):
//   (a) every MGEF, including non-fire supplemental effects;
//   (b) its costliest effect's MGEF (MagicItem::GetAVEffect, the one the engine takes the
//       projectile and the casting art from), fire or not.
// And for every bound fire weapon seed (FireSet.h BoundFireSeeds, bound-weapon stage 1; edge kEdgeBoundArrow, via = the
// seed's MGEF): its MGEF's projectileBase (bound bow MGEFs carry the arrow's PROJ there), and for a bow or crossbow the
// PROJ of every AMMO with the WeapTypeBoundArrow default-object keyword (the arrows a bound-bow script hands out; an
// over-approximation, harmless: the Launch swap needs the player's bound fire weapon as the weapon source).
// Edges, breadth first to a fixed point:
//   MGEF  projectileBase, explosion, impactDataSet, castingArt, dualCastData, a spawn-hazard
//         archetype's associated form; hitEffectArt and hitVisuals' art (RFCT data.artObject);
//         all MGEFs of equipAbility and an associated SPEL, recursively
//   DUAL  pProjectile, pExplosion, pImpactDataSet, pHitEffectArt
//   PROJ  explosionType          EXPL  impactDataSet, impactPlacedObject, spawnProjectile
//   IPDS  every impact            IPCT  hazard, decalTextureSet, decalTextureSet2
//   PROJ  also decalData (its decal TXST; edge kEdgeDecal, as both IPCT texture sets)
//   HAZD  impactDataSet, and all its spell's MGEFs (without making the spell gameplay fire)
// Candidate copies do not authorize a swap: runtime ownership and originating-spell checks remain required.
// Effect shaders are recoloured by runtime shader clones, not these supported form-copy types.
// Copy types: PROJ, EXPL, IPCT, HAZD, ARTO, TXST (kCopyKindTraits). IPDS, DUAL, RFCT, SPEL and MGEF are passed through.
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
        kArt,
        kTextureSet  // a decal texture set of a copied PROJ / IPCT
    };

    /// Every per-kind fact in one table, indexed by the enum (checked below): the form type and the record tag the request
    /// and the copies map use. CopyKindTag, KindOf and the copies map's TypeOf read it, so a new kind is one row
    struct CopyKindTraits
    {
        CopyKind kind;
        RE::FormType formType;
        const char* tag;
    };

    inline constexpr CopyKindTraits kCopyKindTraits[] = {
        { CopyKind::kProjectile, RE::FormType::Projectile, "PROJ" },
        { CopyKind::kExplosion, RE::FormType::Explosion, "EXPL" },
        { CopyKind::kImpact, RE::FormType::Impact, "IPCT" },
        { CopyKind::kHazard, RE::FormType::Hazard, "HAZD" },
        { CopyKind::kArt, RE::FormType::ArtObject, "ARTO" },
        { CopyKind::kTextureSet, RE::FormType::TextureSet, "TXST" },
    };
    inline constexpr std::size_t kCopyKindCount = std::size(kCopyKindTraits);

    [[nodiscard]] constexpr bool CopyKindTraitsInOrder() noexcept
    {
        for (std::size_t i = 0; i < kCopyKindCount; ++i) {
            if (static_cast<std::size_t>(kCopyKindTraits[i].kind) != i) {
                return false;
            }
        }
        return static_cast<std::size_t>(CopyKind::kTextureSet) + 1 == kCopyKindCount;
    }
    static_assert(CopyKindTraitsInOrder(), "kCopyKindTraits: one row per CopyKind, in the enum's order");

    /// "PROJ" / "EXPL" / "IPCT" / "HAZD" / "ARTO" / "TXST"
    [[nodiscard]] const char* CopyKindTag(CopyKind a_kind) noexcept;

    /// The copy kind of a form type, or none (not a copy type)
    [[nodiscard]] std::optional<CopyKind> CopyKindOf(RE::FormType a_type) noexcept;

    /// The form type of a record tag ("PROJ" .. "TXST"), or FormType::None
    [[nodiscard]] RE::FormType CopyFormTypeOf(std::string_view a_tag) noexcept;

    struct FireClosure;

    // C5 (T0-cover): the edges that reached a form, so the self-test can tell a copy that no swap point uses
    enum ClosureEdge : std::uint32_t
    {
        kEdgeOther = 1u << 0,
        kEdgeCastingArt = 1u << 1,           // MGEF.castingArt of an effect not reached from a costliest seed
        kEdgeCastingArtCostliest = 1u << 2,  // MGEF.castingArt from a costliest-effect seed (SetCastingArt shows it)
        kEdgeHitArt = 1u << 3,               // a visual MGEF's hit art, RFCT art or DUAL hit art (the 0x0C swap)
        kEdgeSpawnHazard = 1u << 4,          // a spawn-hazard MGEF that is not a fire effect (no swap point)
        kEdgeSpawnHazardFire = 1u << 5,      // a fire spawn-hazard MGEF (the spawn window)
        kEdgeBoundArrow = 1u << 6,           // a bound fire weapon's arrow PROJ (the Launch swap with the weapon as source)
        kEdgeDecal = 1u << 7                 // a copied PROJ's or IPCT's decal texture set (used through that copy's link)
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

    /// kDataLoaded thread, after BuildFireSets and BuildVisualSets (also kept as LastFireClosure)
    [[nodiscard]] FireClosure BuildFireClosure();

    /// The form's own model path as its record stores it (TESModel::model), or empty
    [[nodiscard]] std::string ModelPathOf(const RE::TESForm* a_form);

    /// The form's defining file is one of our own plugins (HeartOfMagic_BlackFlame*, case ignored)
    [[nodiscard]] bool IsOwnPluginForm(const RE::TESForm* a_form);

    /// "Plugin.esp|00XXXX": the defining file's name (UTF-8) and the local id (12 bits in a light plugin)
    [[nodiscard]] std::string SourceText(const RE::TESForm* a_form);
}
