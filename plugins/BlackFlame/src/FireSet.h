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
// carries the vanilla MagicDamageFire keyword. Only SpellType::kSpell counts
// (no staffs, scrolls, enchantments, powers), and never the Stoke spell itself.

namespace BlackFlame
{
    inline constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";

    void BuildFireSets();

    [[nodiscard]] bool IsFireSpell(const RE::MagicItem* a_spell);

    /// An explosion named by a fire spell's effect (EffectSetting::data.explosion) or
    /// by that effect's projectile (BGSProjectile::data.explosionType).
    [[nodiscard]] bool IsFireExplosion(const RE::BGSExplosion* a_explosion);
}
