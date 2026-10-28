#pragma once

#include "AsciiText.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
// candidates (CandidateIndex, every loaded spell: a fork or port keeps the editor
// ids), and the node takes one only when exactly one is in its school.
// Several, or none: the node is not taken (as before, it leaves the tree).
// No game types, so tools/tree-remap-check runs it.
namespace EditorIdRematch
{
    /// Spells with one editor id: formId and school
    using Candidates = std::vector<std::pair<std::uint32_t, std::string>>;

    /// Every loaded spell by editor id. The case of A-Z is folded, as the engine's editor id
    /// lookup does, so a case-only change still matches and case variants are several spells
    struct CandidateIndex
    {
        void Add(std::string_view editorId, std::uint32_t formId, std::string school)
        {
            byEditorId[AsciiText::ToLower(editorId)].emplace_back(formId, std::move(school));
        }

        /// The spells with that editor id, or null
        [[nodiscard]] const Candidates* Find(std::string_view editorId) const
        {
            const auto it = byEditorId.find(AsciiText::ToLower(editorId));
            return it != byEditorId.end() ? &it->second : nullptr;
        }

        /// Distinct editor ids (case folded)
        [[nodiscard]] std::size_t Size() const { return byEditorId.size(); }

        std::unordered_map<std::string, Candidates> byEditorId;  ///< case-folded editor id -> its spells
    };

    /**
     * The one candidate in the node's school.
     * @param school               the tree school the node is saved under ("Destruction")
     * @param candidatesWithSchool the spells with the node's editor id: formId and school
     * @return its formId; none when no candidate or more than one spell (distinct formIds) is in the school
     */
    [[nodiscard]] inline std::optional<std::uint32_t> PickRematch(
        std::string_view school, const Candidates& candidatesWithSchool)
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
