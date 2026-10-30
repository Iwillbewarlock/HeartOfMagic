#pragma once

// =============================================================================
// SPELL SCHOOL - a spell's magic school as a tree school key ("Destruction" ...),
// from its effects. Moved out of Spells.h/.cpp (HoM add-on plan Task 3): the
// core asks it too.
// =============================================================================

#include <string>

namespace SpellSchool
{
    namespace detail
    {
        inline std::string SchoolKey(RE::ActorValue a_skill)
        {
            switch (a_skill) {
            case RE::ActorValue::kAlteration: return "Alteration";
            case RE::ActorValue::kConjuration: return "Conjuration";
            case RE::ActorValue::kDestruction: return "Destruction";
            case RE::ActorValue::kIllusion: return "Illusion";
            case RE::ActorValue::kRestoration: return "Restoration";
            default: return {};
            }
        }
    }

    // The costliest effect's magic skill; when that effect has none (a spell whose costliest effect is a skill-less
    // script or light effect: game run 2, a healing spell kept "Destruction"), the first effect that has one
    inline std::string Of(const RE::SpellItem* a_spell)
    {
        if (!a_spell) return {};
        const auto* costliest = a_spell->GetCostliestEffectItem();
        if (costliest && costliest->baseEffect) {
            if (auto key = detail::SchoolKey(costliest->baseEffect->GetMagickSkill()); !key.empty()) return key;
        }
        for (const auto* effect : a_spell->effects) {
            if (!effect || !effect->baseEffect) continue;
            if (auto key = detail::SchoolKey(effect->baseEffect->GetMagickSkill()); !key.empty()) return key;
        }
        return {};
    }
}
