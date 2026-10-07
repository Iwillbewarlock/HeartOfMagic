#include "core/NpcSpellsClassTier.h"

namespace NpcSpells {
    ClassTier ClassifyClass(const ClassWeights& c, const Rules& rules)
    {
        int total = 0, magic = 0;
        for (std::size_t i = 0; i < c.w.size(); ++i) {
            total += c.w[i];
            if (i >= kFirstMagicSkill && i < kFirstMagicSkill + kSchoolCount) magic += c.w[i];
        }
        if (total == 0) return ClassTier::NonMage;
        if (magic * 10 >= total * rules.mageMinMagicShareTenths) return ClassTier::Mage;
        if (magic * 10 >= total * rules.hybridMinMagicShareTenths) return ClassTier::Hybrid;
        return ClassTier::NonMage;
    }

    bool IsConjurationClass(const ClassWeights& c)
    {
        constexpr int kConjurationFloor = 3;
        const int conj = c.w[kFirstMagicSkill + static_cast<std::size_t>(School::Conjuration)];
        if (conj < kConjurationFloor) return false;
        for (std::size_t s = 0; s < kSchoolCount; ++s)
            if (c.w[kFirstMagicSkill + s] > conj) return false;
        return true;
    }

    std::array<bool, kSchoolCount> SchoolsWeighted(const ClassWeights& c)
    {
        std::array<bool, kSchoolCount> out{};
        for (std::size_t s = 0; s < kSchoolCount; ++s) out[s] = c.w[kFirstMagicSkill + s] > 0;
        return out;
    }
}
