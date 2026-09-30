#include "treebuilder/TreeBuilderInternal.h"
#include "SimdKernels.h"

#include <hwy/aligned_allocator.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

// =============================================================================
// TIER UTILITIES
// =============================================================================

int TreeBuilder::TierIndex(const std::string& tierName)
{
    for (int i = 0; i < TIER_COUNT; ++i) {
        if (tierName == TIER_NAMES[i]) return i;
    }
    return -1;
}

// =============================================================================
// TREE NODE
// =============================================================================

TreeBuilder::TreeNode TreeBuilder::TreeNode::FromSpell(const json& spell)
{
    TreeNode node;
    node.formId = spell.value("formId", std::string(""));
    node.name = spell.value("name", node.formId);
    node.tier = spell.value("skillLevel", std::string("Unknown"));
    node.school = spell.value("school", std::string("Unknown"));
    node.spellData = spell;
    const std::string plugin = Internal::SpellPlugin(spell);
    if (!Internal::IsBaseGamePlugin(plugin)) node.modPlugin = plugin;
    return node;
}

// A spell is never its own child or prerequisite (see RemoveBadLinks)
void TreeBuilder::TreeNode::AddChild(const std::string& childId)
{
    if (childId == formId) return;
    if (std::find(children.begin(), children.end(), childId) == children.end()) {
        children.push_back(childId);
    }
}

void TreeBuilder::TreeNode::AddPrerequisite(const std::string& prereqId)
{
    if (prereqId == formId) return;
    if (std::find(prerequisites.begin(), prerequisites.end(), prereqId) == prerequisites.end()) {
        prerequisites.push_back(prereqId);
    }
}

json TreeBuilder::TreeNode::ToDict() const
{
    json result;
    result["formId"] = formId;
    result["children"] = children;
    result["prerequisites"] = prerequisites;
    result["tier"] = depth + 1;  // 1-indexed tiers for output

    if (!name.empty()) result["name"] = name;
    if (!tier.empty() && tier != "Unknown") result["skillLevel"] = tier;
    if (!theme.empty()) result["theme"] = theme;
    if (!themes.empty()) result["themes"] = themes;

    return result;
}

void TreeBuilder::LinkNodes(TreeNode& parent, TreeNode& child)
{
    if (&parent == &child || parent.formId == child.formId) return;
    parent.AddChild(child.formId);
    child.AddPrerequisite(parent.formId);
    child.depth = (std::max)(child.depth, parent.depth + 1);
}

// =============================================================================
// BUILD CONFIGURATION
// =============================================================================

TreeBuilder::BuildConfig TreeBuilder::BuildConfig::FromJson(const json& config)
{
    BuildConfig bc;
    bc.seed = config.value("seed", 0);
    bc.maxChildrenPerNode = config.value("max_children_per_node", 3);
    bc.topThemesPerSchool = config.value("top_themes_per_school", 8);
    bc.autoFixUnreachable = config.value("auto_fix_unreachable", true);
    bc.preferVanillaRoots = config.value("prefer_vanilla_roots", true);
    bc.density = config.value("density", 0.6f);
    bc.symmetry = config.value("symmetry", 0.3f);
    bc.commonThemeShare = config.value("common_theme_share", bc.commonThemeShare);

    // Selected roots
    if (config.contains("selected_roots") && config["selected_roots"].is_object()) {
        for (auto& [school, val] : config["selected_roots"].items()) {
            if (val.is_object() && val.contains("formId")) {
                bc.selectedRoots[school] = val["formId"].get<std::string>();
            } else if (val.is_string()) {
                bc.selectedRoots[school] = val.get<std::string>();
            }
        }
    }

    // Grid hint
    if (config.contains("grid_hint") && config["grid_hint"].is_object()) {
        auto& gh = config["grid_hint"];
        BuildConfig::GridHint hint;
        hint.mode = gh.value("mode", std::string("sun"));
        hint.schoolCount = gh.value("schoolCount", 5);
        hint.avgPointsPerSchool = gh.value("avgPointsPerSchool", 0);
        bc.gridHint = hint;
    }

    return bc;
}

// =============================================================================
// INTERNAL SHARED HELPERS
// =============================================================================

bool TreeBuilder::Internal::IsVanillaFormId(const std::string& formIdStr)
{
    try {
        auto val = std::stoul(formIdStr, nullptr, 16);
        return (val >> 24) < 0x05;
    } catch (...) {
        return false;
    }
}

const json* TreeBuilder::Internal::PickRoot(
    const std::unordered_map<std::string, std::vector<json>>& byTier,
    const BuildConfig& config,
    const std::string& school,
    std::mt19937& rng)
{
    // User override
    auto overrideIt = config.selectedRoots.find(school);
    if (overrideIt != config.selectedRoots.end()) {
        for (const auto& [tier, spells] : byTier) {
            for (const auto& s : spells) {
                if (s.value("formId", std::string("")) == overrideIt->second) {
                    return &s;
                }
            }
        }
    }

    // Auto-pick: prefer vanilla from lowest tier
    for (int i = 0; i < TIER_COUNT; ++i) {
        auto it = byTier.find(TIER_NAMES[i]);
        if (it == byTier.end() || it->second.empty()) continue;

        if (config.preferVanillaRoots) {
            std::vector<const json*> vanilla;
            for (const auto& s : it->second) {
                if (IsVanillaFormId(s.value("formId", std::string("")))) {
                    vanilla.push_back(&s);
                }
            }
            if (!vanilla.empty()) {
                std::uniform_int_distribution<int> dist(0, static_cast<int>(vanilla.size()) - 1);
                return vanilla[dist(rng)];
            }
        }

        std::uniform_int_distribution<int> dist(0, static_cast<int>(it->second.size()) - 1);
        return &it->second[dist(rng)];
    }

    return nullptr;
}

std::unordered_map<std::string, TreeBuilder::TreeNode>
TreeBuilder::Internal::RebuildValNodes(const json& schoolData)
{
    std::unordered_map<std::string, TreeNode> valNodes;
    if (!schoolData.contains("nodes") || !schoolData["nodes"].is_array()) {
        return valNodes;
    }
    for (const auto& nd : schoolData["nodes"]) {
        if (!nd.is_object()) continue;
        TreeNode n;
        n.formId = nd.value("formId", std::string(""));
        n.name = nd.value("name", std::string(""));
        n.tier = nd.value("skillLevel", std::string("Unknown"));
        n.depth = nd.value("tier", 1) - 1;
        n.theme = nd.value("theme", std::string(""));
        if (nd.contains("children") && nd["children"].is_array())
            for (const auto& c : nd["children"])
                if (c.is_string()) n.children.push_back(c.get<std::string>());
        if (nd.contains("prerequisites") && nd["prerequisites"].is_array())
            for (const auto& p : nd["prerequisites"])
                if (p.is_string()) n.prerequisites.push_back(p.get<std::string>());
        valNodes[n.formId] = std::move(n);
    }
    return valNodes;
}

void TreeBuilder::Internal::ValidateAndFix(json& treeData, int maxChildren, bool autoFix)
{
    // Self-links and repeated ids first, so the reachability check sees the real links
    const int badLinks = RemoveBadLinks(treeData);
    if (badLinks > 0) {
        logger::warn("TreeBuilder: removed {} self or duplicate links", badLinks);
    }

    if (autoFix) {
        for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
            auto rootId = schoolData.value("root", std::string(""));
            if (rootId.empty()) continue;

            auto valNodes = RebuildValNodes(schoolData);
            int fixes = FixUnreachableNodes(valNodes, rootId, maxChildren);
            if (fixes > 0) {
                json fixedNodes = json::array();
                for (const auto& [fid, n] : valNodes)
                    fixedNodes.push_back(n.ToDict());
                schoolData["nodes"] = fixedNodes;
            }
        }
    }

    int totalNodes = 0, reachableNodes = 0;
    bool allValid = true;
    for (auto& [schoolName, schoolData] : treeData["schools"].items()) {
        auto rootId = schoolData.value("root", std::string(""));
        if (rootId.empty()) continue;

        auto valNodes = RebuildValNodes(schoolData);
        totalNodes += static_cast<int>(valNodes.size());
        auto unlocked = SimulateUnlocks(valNodes, rootId);
        reachableNodes += static_cast<int>(unlocked.size());
        if (static_cast<int>(unlocked.size()) != static_cast<int>(valNodes.size()))
            allValid = false;
    }

    treeData["validation"] = {
        {"all_valid", allValid},
        {"total_nodes", totalNodes},
        {"reachable_nodes", reachableNodes},
        {"bad_links_removed", badLinks}
    };
}

// =============================================================================
// HIGH-LEVEL API
// =============================================================================

TreeBuilder::BuildResult TreeBuilder::Build(
    const std::string& command,
    const std::vector<json>& spells,
    const json& configJson)
{
    auto config = BuildConfig::FromJson(configJson);

    logger::info("TreeBuilder::Build command='{}', spells={}, seed={}",
                 command, spells.size(), config.seed);

    BuildResult result;
    // Classic is the one builder (2026-09-27): the Tree, Graph, Thematic and
    // Oracle builders were removed, so their commands now fail like any other
    // unknown command.
    if (command == "build_tree_classic") {
        result = BuildClassic(spells, config);
    } else {
        result.success = false;
        result.error = "Unknown build command: " + command;
        return result;
    }

    // After the build: links between the schools, handed to the
    // layout as data. See TreeBuilderBridges.cpp for why they stay out of the trees.
    if (result.success && result.treeData.is_object()) {
        auto links = ComputeCrossSchoolBridges(spells);
        result.treeData["bridges"] = std::move(links["bridges"]);
        result.treeData["schoolLinks"] = std::move(links["schoolLinks"]);
        logger::info("TreeBuilder: {} cross school bridges", result.treeData["bridges"].size());
    }
    return result;
}
