#pragma once

#include "Common.h"

// =============================================================================
// FIRE SET - which spells and explosions count as the player's fire magic
// =============================================================================
// Built once on the game thread at kDataLoaded, before any hook is installed;
// immutable afterwards, so the hooks may read it from any thread.
//
// A spell is fire when Heart of Magic's spell_catalog.json lists "fire" in its
// "elements"; a spell the catalog does not list is fire when one of its effects
// is a fire MGEF (the vanilla MagicDamageFire keyword, or fire resistance as its
// resist value). Only SpellType::kSpell counts (no staffs, scrolls, enchantments,
// powers), and never the Stoke spell itself.

namespace BlackFlame
{
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";

    void BuildFireSets();

    struct FireSetCounts
    {
        bool catalogRead = false;
        std::size_t catalogFire = 0;
        std::size_t keywordFire = 0;
        std::size_t total = 0;
        std::size_t effects = 0;
        std::size_t explosions = 0;
    };

    /// What BuildFireSets found (for the self-test)
    [[nodiscard]] FireSetCounts GetFireSetCounts() noexcept;

    [[nodiscard]] bool IsFireSpell(const RE::MagicItem* a_spell);

    /// A fire MGEF of a fire spell: MagicDamageFire keyword or fire resistance as its resist
    /// value; a catalog-fire spell with none of those contributes its costliest effect. Not a
    /// verdict alone: a scroll, staff or enchantment can carry the same MGEF, so every caller
    /// also requires IsFireSpell of the item that cast it.
    [[nodiscard]] bool IsFireEffect(const RE::EffectSetting* a_effect);

    /// An explosion named by a fire effect (EffectSetting::data.explosion) or by that
    /// effect's projectile (BGSProjectile::data.explosionType).
    [[nodiscard]] bool IsFireExplosion(const RE::BGSExplosion* a_explosion);
}
