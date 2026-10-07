#pragma once
#include "core/NpcSpellsRules.h"
#include <array>
#include <cstdint>

namespace NpcSpells {
    // The 18 CLASS_DATA::SkillWeights in record order (RE/T/TESClass.h:36-59); magic schools are indices 12..16.
    struct ClassWeights { std::array<std::uint8_t, 18> w{}; };
    inline constexpr std::size_t kFirstMagicSkill = 12;

    ClassTier ClassifyClass(const ClassWeights& weights, const Rules& rules);      // integer compare
    bool IsConjurationClass(const ClassWeights& weights);                         // conj >= 3 and >= every other magic weight
    std::array<bool, kSchoolCount> SchoolsWeighted(const ClassWeights& weights);  // weight > 0 per school (hybrid scope)
}
