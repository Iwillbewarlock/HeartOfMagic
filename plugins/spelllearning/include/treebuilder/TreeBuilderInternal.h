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

    // Rebuild validation node map from serialized JSON
    std::unordered_map<std::string, TreeNode>
    RebuildValNodes(const json& schoolData);

    // Run validation + auto-fix + stats on tree data
    void ValidateAndFix(json& treeData, int maxChildren, bool autoFix);

    // Drop every node's links to itself and repeated ids from its children and
    // prerequisites, in place (other node fields untouched). Returns how many went.
    int RemoveBadLinks(json& treeData);

    // The spell's plugin in lower case ("plugin", else the persistentId's
    // plugin part), and whether it is the game or one of its DLC
    std::string SpellPlugin(const json& spell);
    bool IsBaseGamePlugin(const std::string& lowerPlugin);

    // Both spells come from the same mod (not the game or its DLC, whose
    // spells are the tree's frame rather than one author's set)
    bool SharesModPlugin(const TreeNode& a, const TreeNode& b);

    // Classic's parent score for kind and element (TreeBuilderTypeGrouping.cpp):
    // a shared element.* trait (the elements the tag librarian hands on) and a
    // shared kind.* trait, each weighed by how rare it is in the school, and a
    // penalty for two spells made of different things. Built once per school;
    // strength is BuildConfig::typeGrouping, 0 = the flat 1.2.0 element bonus.
    class TypeGrouping
    {
    public:
        TypeGrouping(const std::unordered_map<std::string, TreeNode>& nodes, float strength);

        // What `parent` adds to its score as `child`'s parent
        float Score(const TreeNode& child, const TreeNode& parent) const;

    private:
        struct Types {
            std::vector<std::string> elements;
            std::vector<std::string> kinds;
        };

        // Rarity of the rarest trait the two lists share, -1 when they share none
        float BestSharedRarity(const std::vector<std::string>& a, const std::vector<std::string>& b) const;

        float m_strength;
        std::unordered_map<std::string, Types> m_types;   // formId -> its element and kind traits
        std::unordered_map<std::string, float> m_rarity;  // trait -> 0 (every spell) .. 1 (rare)
    };

    // Classic's parent for `node` among `available` (tier index -> nodes that may
    // still take a child), TreeBuilderClassic.cpp. rng adds the build's random
    // jitter to each score; nullptr = none (the graft, TreeGraft.cpp, which must
    // give the same tree for the same input). parentBonus (formId -> points) adds
    // a caller's own term per candidate; nullptr = none.
    TreeNode* FindBestClassicParent(
        TreeNode& node,
        std::unordered_map<int, std::vector<TreeNode*>>& available,
        int tierIdx,
        int maxChildren,
        const SimilarityMatrix& sims,
        const TypeGrouping& grouping,
        std::mt19937* rng,
        const std::unordered_map<std::string, float>* parentBonus = nullptr);

}  // namespace TreeBuilder::Internal
