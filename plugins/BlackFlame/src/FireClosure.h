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

    struct ClosureForm
    {
        RE::TESForm* form = nullptr;
        CopyKind kind = CopyKind::kProjectile;
        RE::FormID viaSpell = 0;          // the first spell whose walk reached it
        bool reachedByCostliest = false;  // some walk from a costliest-effect seed reached it
    };

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
    };

    /// kDataLoaded thread, after BuildFireSets
    [[nodiscard]] FireClosure BuildFireClosure();

    /// The form's own model path as its record stores it (TESModel::model), or empty
    [[nodiscard]] std::string ModelPathOf(const RE::TESForm* a_form);

    /// The form's defining file is one of our own plugins (HeartOfMagic_BlackFlame*, case ignored)
    [[nodiscard]] bool IsOwnPluginForm(const RE::TESForm* a_form);

    /// "Plugin.esp|00XXXX": the defining file's name (UTF-8) and the local id (12 bits in a light plugin)
    [[nodiscard]] std::string SourceText(const RE::TESForm* a_form);
}
