#pragma once

#include "treebuilder/GraftCandidates.h"
#include "treebuilder/TreeBuilder.h"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// TREE GRAFT - new spells join a saved spell tree; no existing node moves
// (no game types: tools/graft-check.cpp runs it offline; the design is
// docs/superpowers/specs/2026-10-28-tree-graft-design.md, section 3)
// =============================================================================
//
// Each candidate the rules take (IsTreeCandidate) goes into its school:
//  - Tier as the build gives it (SpellTiers over the school's live nodes and the
//    new spells, by level or with tiers_by_cost by cost rank); existing nodes keep
//    their saved tier.
//  - Parent: Classic's own scoring (FindBestClassicParent) without its random
//    jitter, over the school's live nodes - a node is live when the scan has its
//    spell: by its persistentId, or by its formId when it has none. A node of a
//    plugin that is gone is never a parent and stays in the file as it is. The
//    scoring inputs (similarities, themes, type grouping, mod tags) are rebuilt
//    from the scan for the live nodes and the new spells together; they only
//    score. The child cap is the tree's own largest child count, at least
//    maxChildrenPerNode. Lowest tier first, so one new spell may hang under
//    another.
//  - Room (the distance term): each candidate parent gets kRoomWeight (2) points
//    for every node short of kRoomCrowd (6) within kRoomRadius (110) of the point
//    its new child would take - at most 12 points, about a tier preference (10),
//    below a shared theme (15-25) or element (30+), so it only parts near-equal
//    parents. A parent in a crowded spot would have its child pushed far off by
//    the declutter: a long link across the school. TreeGraft.cpp has the numbers.
//  - The new node: formId, persistentId, name, tier (from 1), school, skillLevel
//    and theme when known, children [], prerequisites = softPrereqs = [parent],
//    softNeeded 1, hardPrereqs [] (no locks, no requiredXP), and a provisional x, y
//    one node spacing beside the parent, away from the globe's centre, fanned out
//    from its new siblings and kept inside its tier's ring of the school. The
//    declutter (fixed existing nodes, no spread) settles it afterwards.
//  - The parent's children gain the new formId. No other field of any existing
//    node, and no school field, changes.
//
// The tree's own build settings win over `config` where the tree records them:
// config.tier_mode ("cost" = tiersByCost) and config.type_grouping.
//
// Not placed - in skippedNoSchool, the tree as it was: a spell of a school the
// tree has no live node in (a mod brings the first Alteration spells of a tree
// built without them), or one no live node of its school can take. The lists hold
// each spell's persistentId (its formId when it has none), the seen list's id.
// Deterministic: the same tree, scan and candidates give the same tree.
// O(new x live nodes of the school) for the parent and room scores, plus the
// build's own O(n^2) similarity matrix per school touched.
namespace TreeGraft
{
    template <class Json>
    struct BasicGraftResult
    {
        Json tree;
        std::vector<std::string> grafted, leftOut, skippedNoSchool;
    };

    using GraftResult = BasicGraftResult<nlohmann::json>;
    /// The file's own key order kept (UIManagerTreeState.cpp reads the tree as ordered_json:
    /// the school order decides which school's node of a two-school spell applies last)
    using OrderedGraftResult = BasicGraftResult<nlohmann::ordered_json>;

    /// `scanSpells` is the current scan (every spell, for the live nodes' spell data);
    /// `candidates` the spells to judge (NewSpells), which need not point into it
    GraftResult Graft(const nlohmann::json& tree, const std::vector<nlohmann::json>& scanSpells,
        const std::vector<const nlohmann::json*>& candidates, const PlayerRules& rules,
        const TreeBuilder::BuildConfig& config);

    OrderedGraftResult Graft(const nlohmann::ordered_json& tree, const std::vector<nlohmann::json>& scanSpells,
        const std::vector<const nlohmann::json*>& candidates, const PlayerRules& rules,
        const TreeBuilder::BuildConfig& config);
}
