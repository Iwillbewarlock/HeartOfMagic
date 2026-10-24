#pragma once

#include <cstdint>
#include <vector>

namespace BlackFlame::VisualEquipRules
{
    using FormID = std::uint32_t;

    struct SelectedRoot
    {
        FormID id = 0;
        bool visual = false;
    };

    // A shared ability has no originating spell identity. Accept an equipped visual
    // owner only when no equipped nonvisual owner can account for the same ability.
    [[nodiscard]] inline bool Allows(const std::vector<FormID>& a_owners,
        const std::vector<SelectedRoot>& a_selected)
    {
        bool visualOwner = false;
        // O(selected roots * owners); selected roots are the small fixed caster-slot
        // set. Index owners if this helper later accepts arbitrary large selections.
        for (const auto& selected : a_selected) {
            if (selected.id == 0) {
                continue;
            }
            for (const auto owner : a_owners) {
                if (owner != 0 && owner == selected.id) {
                    if (!selected.visual) {
                        return false;
                    }
                    visualOwner = true;
                    break;
                }
            }
        }
        return visualOwner;
    }

}
