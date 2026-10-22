#pragma once

// =============================================================================
// EQUIP ABILITY RULES TESTS - plugins/BlackFlame/src/EquipAbilityRules.h: a fire parent's equip ability is collected and
// passes the art check (Fire Storm's 080E34), a non-fire parent's is not (the Illusion master spell's 0225AE), nor one
// with no hit art, ability 0 is never collected, a burnable effect passes whatever the set, spell 0 never passes
// =============================================================================
// Included by main.cpp.

#include "../../plugins/BlackFlame/src/EquipAbilityRules.h"

#include <cstdint>
#include <vector>

namespace EquipAbilityRulesTests
{
    namespace E = BlackFlame::EquipAbilityRules;

    template <class Check>
    void Run(Check&& a_check)
    {
        constexpr std::uint32_t kFireStormAbility = 0x00080E34;
        constexpr std::uint32_t kIllusionAbility = 0x000225AE;
        constexpr std::uint32_t kNoArtAbility = 0xFE000812;  // a fire parent's ability with no hit art

        const std::vector<E::ParentFacts> parents{
            { true, kFireStormAbility, true },
            { false, kIllusionAbility, true },
            { true, kNoArtAbility, false },
            { true, 0, true },
            { false, 0, false },
        };
        const auto abilities = E::CollectFireEquipAbilities(parents);

        a_check((abilities.count(kFireStormAbility) != 0) && E::PassesArtFireCheck(false, kFireStormAbility, abilities),
            "equip ability: a fire parent's ability is collected and passes the art check");
        a_check(!(abilities.count(kIllusionAbility) != 0) && !E::PassesArtFireCheck(false, kIllusionAbility, abilities),
            "equip ability: a non-fire parent's ability (the Illusion master spell's) is not collected and does not pass");
        a_check(abilities.count(kNoArtAbility) == 0, "equip ability: a fire parent's ability with no hit art is not collected");
        a_check(abilities.count(0) == 0 && abilities.size() == 1, "equip ability: ability 0 is never collected");
        a_check(E::PassesArtFireCheck(true, 0, {}) && E::PassesArtFireCheck(true, kIllusionAbility, abilities),
            "equip ability: a burnable effect passes whatever the set");
        a_check(!E::PassesArtFireCheck(false, 0, abilities), "equip ability: spell 0 does not pass");
    }
}
