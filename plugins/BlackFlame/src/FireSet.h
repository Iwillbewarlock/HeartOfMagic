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

    /// The effect burns grey: a_spell is a fire spell and a_effect is one of the fire MGEFs
    /// (MagicDamageFire keyword or fire resistance) - or, for a catalog-fire spell with none of
    /// those, its costliest effect, for THAT spell only. A scroll, staff or enchantment carrying
    /// the same MGEF does not count (not a fire spell).
    [[nodiscard]] bool IsBurnable(const RE::EffectSetting* a_effect, const RE::MagicItem* a_spell);

    /// An explosion named by a fire MGEF (EffectSetting::data.explosion) or by its projectile
    /// (BGSProjectile::data.explosionType); one named only by a fallback (costliest) effect counts
    /// only when a_casterSpell is the spell that contributed it.
    [[nodiscard]] bool IsFireExplosion(const RE::BGSExplosion* a_explosion, const RE::MagicItem* a_casterSpell);

    /// A hazard whose spell (BGSHazardData::spell) is a fire spell or carries a fire MGEF (hazard
    /// spells need not be of the counted kSpell type)
    [[nodiscard]] bool IsFireHazard(const RE::BGSHazard* a_hazard);
}
