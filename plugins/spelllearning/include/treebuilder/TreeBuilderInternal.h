#pragma once

#include "treebuilder/TreeBuilder.h"

#include <random>

// =============================================================================
// Internal helpers shared across TreeBuilder implementation files.
// NOT part of the public API — only included by TreeBuilder*.cpp files.
// =============================================================================

namespace TreeBuilder::Internal
{
    // Check if a formId is likely vanilla (low load order, first 5 slots)
    bool IsVanillaFormId(const std::string& formIdStr);

    // Pick root spell for a school (checks user overrides, prefers vanilla)
    const json* PickRoot(
        const std::unordered_map<std::string, std::vector<json>>& byTier,
        const BuildConfig& config,
        const std::string& school,
        std::mt19937& rng);

    // Sort spells by tier then magicka cost then name
    void SortByTierAndCost(std::vector<json>& spells);

    // Rebuild validation node map from serialized JSON
    std::unordered_map<std::string, TreeNode>
    RebuildValNodes(const json& schoolData);

    // Run validation + auto-fix + stats on tree data (shared by all builders)
    void ValidateAndFix(json& treeData, int maxChildren, bool autoFix);

    // Both spells made of the same thing (both blood, both water): an element.*
    // trait in common - the elements the tag librarian hands on. Classic and
    // Tree give a parent like that a bonus on their own scale. Graph, Thematic
    // and Oracle do not: measured, it changed nothing there (Thematic already
    // branches by element themes; Oracle and Graph were within their noise).
    inline bool SharesElement(const TreeNode& a, const TreeNode& b)
    {
        const auto at = a.spellData.find("traits");
        const auto bt = b.spellData.find("traits");
        if (at == a.spellData.end() || bt == b.spellData.end() || !at->is_array() || !bt->is_array()) return false;
        for (const auto& x : *at) {
            if (!x.is_string() || !x.get_ref<const std::string&>().starts_with("element.")) continue;
            for (const auto& y : *bt) {
                if (y == x) return true;
            }
        }
        return false;
    }

}  // namespace TreeBuilder::Internal
