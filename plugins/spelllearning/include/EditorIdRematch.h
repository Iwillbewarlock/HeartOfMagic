#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// EDITOR ID REMATCH - a node whose plugin was renamed, merged or compacted
// =============================================================================
//
// A saved node's persistentId ("Plugin.esp|0x000800") names nothing once its plugin
// is renamed, merged into another or ESL-compacted. The node also carries the
// spell's editor id (SchoolBridges.applyToOutput bakes it); the spells with that
// editor id (RE::TESForm::LookupByEditorID, needs powerofthree's Tweaks) are the
// candidates, and the node takes one only when exactly one is in its school.
// Several, or none: the node is not taken (as before, it leaves the tree).
// No game types, so tools/tree-remap-check runs it.
namespace EditorIdRematch
{
    /**
     * The one candidate in the node's school.
     * @param school               the tree school the node is saved under ("Destruction")
     * @param candidatesWithSchool the spells with the node's editor id: formId and school
     * @return its formId; none when no candidate or more than one spell (distinct formIds) is in the school
     */
    [[nodiscard]] inline std::optional<std::uint32_t> PickRematch(
        std::string_view school, const std::vector<std::pair<std::uint32_t, std::string>>& candidatesWithSchool)
    {
        std::optional<std::uint32_t> picked;
        for (const auto& [formId, candidateSchool] : candidatesWithSchool) {
            if (candidateSchool != school) continue;
            if (picked && *picked != formId) return std::nullopt;
            picked = formId;
        }
        return picked;
    }
}
