#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

// =============================================================================
// EQUIP ABILITY RULES - a fire MGEF's equip ability (EffectSetting data.equipAbility: the body art a spell carries while
// equipped, e.g. Fire Storm's AbFireStormCastBodyFX) counts as fire for the hit art swap, though its own holder MGEF has
// no fire keyword; no engine type (C++17: tools/blackflame-cosave-check tests them, EquipAbilityRulesTests.h)
// =============================================================================
// FireSet.cpp BuildFireSets fills ParentFacts for every MGEF and keeps the collected set; HitEffectHook.cpp Decide asks
// PassesArtFireCheck for the art slot only. RE::FormID is std::uint32_t.

namespace BlackFlame::EquipAbilityRules
{
    struct ParentFacts
    {
        bool fireEffect = false;         // FireSet IsFireEffect: a fire MGEF of a fire spell
        std::uint32_t equipAbility = 0;  // its data.equipAbility, 0 when none
        bool abilityHasHitArt = false;   // an effect of that ability has a hit art (nothing to swap or refresh otherwise)
    };

    /// The equip abilities of fire parents that carry a hit art
    [[nodiscard]] inline std::unordered_set<std::uint32_t> CollectFireEquipAbilities(const std::vector<ParentFacts>& a_parents)
    {
        std::unordered_set<std::uint32_t> abilities;
        for (const auto& parent : a_parents) {
            if (parent.fireEffect && parent.equipAbility != 0 && parent.abilityHasHitArt) {
                abilities.insert(parent.equipAbility);
            }
        }
        return abilities;
    }

    /// The art slot's fire check: a burnable effect, or an effect of a fire equip ability
    [[nodiscard]] inline bool PassesArtFireCheck(bool a_burnable, std::uint32_t a_spell,
        const std::unordered_set<std::uint32_t>& a_fireAbilities)
    {
        return a_burnable || (a_spell != 0 && a_fireAbilities.find(a_spell) != a_fireAbilities.end());
    }
}
