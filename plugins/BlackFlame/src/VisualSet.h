#pragma once

#include "Common.h"

#include <unordered_set>
#include <vector>

namespace BlackFlame
{
    // Build once after catalog readiness, before the first game load and hooks; all returned collections are immutable afterwards.
    // Only catalog-tagged, ordinary Spell roots qualify. Every MGEF of a root and its recursively
    // associated Cloak spells is a copy candidate. Missing/empty catalog input fails closed.
    void BuildVisualSets(const std::unordered_set<RE::FormID>& a_catalogFire);

    // Runtime admission is stricter than copy candidacy: a spell reachable from any non-catalog
    // ordinary Spell root is refused, including a catalog root reused as such a child. An ActiveEffect
    // names its immediate spell, not its originating cloak, so that ambiguity must keep the original.
    // These predicates do not establish caster ownership or buff state.
    [[nodiscard]] bool IsVisualSpell(const RE::MagicItem* a_spell);
    [[nodiscard]] bool IsVisualEffect(const RE::EffectSetting* a_effect, const RE::MagicItem* a_spell);

    // Static copy candidates include ambiguous spells; making a private copy never authorizes its use.
    [[nodiscard]] const std::vector<RE::SpellItem*>& VisualSpellList() noexcept;
    [[nodiscard]] bool IsVisualEffectForm(const RE::EffectSetting* a_effect);
    [[nodiscard]] RE::BGSArtObject* CostliestVisualCastingArtOf(const RE::MagicItem* a_spell);

    // An equip ability is only a candidate, never admitted as a spell by that relationship. Its caller
    // must establish a currently equipped visual parent and reject competing non-visual parents.
    [[nodiscard]] bool IsVisualEquipAbility(const RE::MagicItem* a_ability);

    // All ordinary root spells that name this ability, directly or through Cloak descendants, including
    // non-catalog roots. Sorted, unique FormIDs; empty when no static owner is known.
    [[nodiscard]] const std::vector<RE::FormID>& VisualEquipOwners(const RE::MagicItem* a_ability) noexcept;
}
