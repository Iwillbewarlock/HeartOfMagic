#pragma once

#include "Common.h"

// =============================================================================
// FIRE SET - which spells and explosions count as the player's fire magic
// =============================================================================
// Built once on the game thread at kDataLoaded, before any hook is installed;
// immutable afterwards, so the hooks may read it from any thread.
//
// Fire (v2, user decision 2026-10-16, reversing round 8b): a spell is fire when one
// of its effects is a fire MGEF (the vanilla MagicDamageFire keyword or fire
// resistance as its resist value), OR when Heart of Magic's spell_catalog.json tags
// it "fire". A fire-MGEF spell's fire effects are its fire MGEFs; a catalog-only
// fire spell's fire effects are all of its effects (it has no fire MGEF to pick).
// The v2 copies change only visuals, so a catalog false positive costs only a grey
// look. Only
// SpellType::kSpell counts (no staffs, scrolls, enchantments, powers), and never
// the Stoke spell itself. The fire explosions and hazards are those the fire effects
// name: their explosions (own and projectile's), impact data sets' hazards,
// explosions' placed hazards and a spawn-hazard archetype's form.
//
// Cloak (v2 expansion, R3): the spell a fire Cloak-archetype MGEF names as its associated
// form (the damage spell the cloak casts on whatever comes near) is a fire spell whatever its
// SpellType, and every effect of it is a fire effect - so the hit art and shader the cloak's
// spell puts on a target count as the player's fire. Repeated until no new spell is added.

namespace BlackFlame
{
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";

    void BuildFireSets();

    struct FireSetCounts
    {
        bool catalogRead = false;
        std::size_t catalogFire = 0;  // spells the catalog tags fire
        std::size_t catalogOnly = 0;  // of those, without a fire effect: fire by the tag alone (v2)
        std::size_t cloakAssociated = 0;  // spells added as a fire cloak's associated spell (any SpellType)
        std::size_t total = 0;        // fire spells
        std::size_t effects = 0;
        std::size_t explosions = 0;
        std::size_t hazards = 0;
    };

    /// What BuildFireSets found (for the self-test)
    [[nodiscard]] FireSetCounts GetFireSetCounts() noexcept;

    [[nodiscard]] bool IsFireSpell(const RE::MagicItem* a_spell);

    /// Every fire spell, by FormID (kDataLoaded thread, after BuildFireSets; the copy closure's seeds)
    [[nodiscard]] std::vector<RE::SpellItem*> FireSpellList();

    /// The effect burns grey: a_spell is a fire spell and a_effect is one of its fire MGEFs. A
    /// scroll, staff or enchantment carrying the same MGEF does not count (not a fire spell).
    [[nodiscard]] bool IsBurnable(const RE::EffectSetting* a_effect, const RE::MagicItem* a_spell);

    /// A fire MGEF of some fire spell
    [[nodiscard]] bool IsFireEffect(const RE::EffectSetting* a_effect);

    /// A fire MGEF by the rule alone (keyword or fire resistance), whatever spell carries it
    [[nodiscard]] bool IsFireMgefAny(const RE::EffectSetting* a_effect);

    /// An explosion named by a fire MGEF (EffectSetting::data.explosion) or by its projectile
    /// (BGSProjectile::data.explosionType)
    [[nodiscard]] bool IsFireExplosion(const RE::BGSExplosion* a_explosion);

    /// A hazard a fire MGEF can leave (see the top), or one whose own spell (BGSHazardData::spell)
    /// is a fire spell or carries a fire MGEF (hazard spells need not be of the counted kSpell type)
    [[nodiscard]] bool IsFireHazard(const RE::BGSHazard* a_hazard);
}
