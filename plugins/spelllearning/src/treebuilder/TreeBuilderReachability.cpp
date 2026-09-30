#include "treebuilder/TreeBuilderInternal.h"

#include <algorithm>
#include <set>

// TreeBuilderReachability - whether every spell of a built tree can be unlocked from its
// root, and the repair that links the ones that cannot. Split out of TreeBuilderThemes.cpp
// (600-line limit).

// =============================================================================
// TREE VALIDATION
// =============================================================================

std::unordered_set<std::string> TreeBuilder::SimulateUnlocks(
    const std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId)
{
    std::unordered_set<std::string> unlocked;
    if (!nodes.contains(rootId)) return unlocked;

    unlocked.insert(rootId);

    // Fixed-point iteration: keep unlocking until no new unlocks
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [fid, node] : nodes) {
            if (unlocked.contains(fid)) continue;

            // Node unlocks when ALL prerequisites are unlocked
            bool allPrereqsMet = true;
            for (const auto& prereq : node.prerequisites) {
                if (!unlocked.contains(prereq)) {
                    allPrereqsMet = false;
                    break;
                }
            }

            if (allPrereqsMet && !node.prerequisites.empty()) {
                unlocked.insert(fid);
                changed = true;
            }
        }
    }

    return unlocked;
}

std::vector<std::string> TreeBuilder::FindUnreachableNodes(
    const std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId)
{
    auto unlocked = SimulateUnlocks(nodes, rootId);

    std::vector<std::string> unreachable;
    for (const auto& [fid, node] : nodes) {
        if (!unlocked.contains(fid)) {
            unreachable.push_back(fid);
        }
    }
    return unreachable;
}

int TreeBuilder::FixUnreachableNodes(
    std::unordered_map<std::string, TreeNode>& nodes,
    const std::string& rootId,
    int maxChildren)
{
    int totalFixes = 0;

    for (int pass = 0; pass < 20; ++pass) {
        auto unreachable = FindUnreachableNodes(nodes, rootId);
        if (unreachable.empty()) break;

        bool fixedAny = false;

        for (const auto& fid : unreachable) {
            auto& node = nodes[fid];

            // Strategy 1: Remove blocking prerequisites
            // Find prereqs that are themselves unreachable
            std::vector<std::string> blockingPrereqs;
            auto currentUnlocked = SimulateUnlocks(nodes, rootId);

            for (const auto& prereq : node.prerequisites) {
                if (!currentUnlocked.contains(prereq)) {
                    blockingPrereqs.push_back(prereq);
                }
            }

            if (!blockingPrereqs.empty()) {
                for (const auto& bp : blockingPrereqs) {
                    // Remove this prerequisite
                    node.prerequisites.erase(
                        std::remove(node.prerequisites.begin(), node.prerequisites.end(), bp),
                        node.prerequisites.end());
                    // Also remove from parent's children
                    if (nodes.contains(bp)) {
                        auto& parent = nodes[bp];
                        parent.children.erase(
                            std::remove(parent.children.begin(), parent.children.end(), fid),
                            parent.children.end());
                    }
                }
                totalFixes++;
                fixedAny = true;
                continue;
            }

            // Strategy 2: If no prerequisites at all, connect to root or nearest available
            if (node.prerequisites.empty()) {
                // Find best parent among reachable nodes
                std::string bestParent;
                int bestChildCount = std::numeric_limits<int>::max();

                for (const auto& [rid, rnode] : nodes) {
                    if (!currentUnlocked.contains(rid)) continue;
                    if (rid == fid) continue;
                    if (static_cast<int>(rnode.children.size()) < maxChildren &&
                        static_cast<int>(rnode.children.size()) < bestChildCount) {
                        bestChildCount = static_cast<int>(rnode.children.size());
                        bestParent = rid;
                    }
                }

                if (!bestParent.empty()) {
                    LinkNodes(nodes[bestParent], node);
                    totalFixes++;
                    fixedAny = true;
                } else {
                    // Last resort: connect to root (even if over capacity)
                    LinkNodes(nodes[rootId], node);
                    totalFixes++;
                    fixedAny = true;
                }
            }
        }

        if (!fixedAny) break;
    }

    return totalFixes;
}
