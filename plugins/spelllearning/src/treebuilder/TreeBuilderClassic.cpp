#include "treebuilder/TreeBuilderInternal.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <random>

using namespace TreeBuilder::Internal;

namespace
{
    // The element and kind scores (same element, same kind, different
    // elements) are in TreeBuilderTypeGrouping.cpp (TypeGrouping).

    // A parent from the same mod: one author's spells were made as a set and
    // read best side by side, so they are drawn together on a branch. Below
    // the element bonus, so a mod's fire spell still goes to fire first.
    constexpr float kSameModBonus = 20.0f;

    // The spell's Magicka cost when it has one above 0 (the scan writes a
    // number, or a numeric string)
    std::optional<float> SpellMagickaCost(const json& spell)
    {
        const auto it = spell.find("magickaCost");
        if (it == spell.end()) return std::nullopt;
        float cost = 0.0f;
        if (it->is_number()) {
            cost = it->get<float>();
        } else if (it->is_string()) {
            try {
                cost = std::stof(it->get<std::string>());
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
        if (!(cost > 0.0f)) return std::nullopt;
        return cost;
    }

    // Tiers by Magicka cost (BuildConfig::tiersByCost): formId -> tier index.
    // The school's spells with a cost, cheapest first, cut into TIER_COUNT parts
    // of about the same size by rank; a group of equal costs goes to the tier its
    // first spell's rank falls in, so equal costs never split. Ranks, not the
    // numbers: mods put costs ten times the game's. Spells without a cost are
    // left out (they keep their level's tier).
    std::unordered_map<std::string, int> CostTiers(const std::vector<const json*>& schoolSpells)
    {
        std::vector<std::pair<float, std::string>> costed;
        for (const auto* spell : schoolSpells) {
            const auto cost = SpellMagickaCost(*spell);
            if (cost) costed.emplace_back(*cost, spell->value("formId", std::string("")));
        }
        std::sort(costed.begin(), costed.end());

        std::unordered_map<std::string, int> tiers;
        const std::size_t count = costed.size();
        std::size_t start = 0;
        while (start < count) {
            std::size_t end = start;
            while (end + 1 < count && costed[end + 1].first == costed[start].first) ++end;
            const int tier = (std::min)(TreeBuilder::TIER_COUNT - 1,
                static_cast<int>(start * TreeBuilder::TIER_COUNT / count));
            for (std::size_t i = start; i <= end; ++i) tiers[costed[i].second] = tier;
            start = end + 1;
        }
        return tiers;
    }

    // A spell's tier: its cost tier when it has one, else its level's (unknown -> Novice)
    int TierOf(const json& spell, const std::unordered_map<std::string, int>& costTiers)
    {
        const auto cost = costTiers.find(spell.value("formId", std::string("")));
        return cost != costTiers.end() ? cost->second
                                       : (std::max)(0, TreeBuilder::TierIndex(spell.value("skillLevel", std::string(""))));
    }
}

std::unordered_map<std::string, int> TreeBuilder::SpellTiers(const std::vector<const json*>& spells, bool tiersByCost)
{
    std::unordered_map<std::string, std::vector<const json*>> bySchool;
    for (const auto* spell : spells) bySchool[spell->value("school", std::string(""))].push_back(spell);

    std::unordered_map<std::string, int> tiers;
    for (const auto& [school, schoolSpells] : bySchool) {
        const auto costTiers = tiersByCost ? CostTiers(schoolSpells) : std::unordered_map<std::string, int>{};
        for (const auto* spell : schoolSpells) tiers[spell->value("formId", std::string(""))] = TierOf(*spell, costTiers);
    }
    return tiers;
}

std::unordered_map<std::string, int> TreeBuilder::SpellTiers(const std::vector<json>& spells, bool tiersByCost)
{
    std::vector<const json*> pointers;
    pointers.reserve(spells.size());
    for (const auto& spell : spells) pointers.push_back(&spell);
    return SpellTiers(pointers, tiersByCost);
}

// =============================================================================
// CLASSIC BUILDER — Tier-First Tree Construction
// =============================================================================

// Find best parent for a node in Classic mode (TreeBuilderInternal.h; the graft runs it too)
TreeBuilder::TreeNode* TreeBuilder::Internal::FindBestClassicParent(
    TreeBuilder::TreeNode& node,
    std::unordered_map<int, std::vector<TreeBuilder::TreeNode*>>& available,
    int tierIdx,
    int maxChildren,
    const TreeBuilder::SimilarityMatrix& sims,
    const TypeGrouping& grouping,
    std::mt19937* rng,
    const std::unordered_map<std::string, float>* parentBonus)
{
    // Collect candidates from lower tiers
    std::vector<std::pair<TreeBuilder::TreeNode*, int>> candidates;

    for (int tierDist = 1; tierDist <= tierIdx + 1; ++tierDist) {
        int targetD = tierIdx - tierDist;
        if (targetD < 0) break;

        auto it = available.find(targetD);
        if (it != available.end()) {
            for (auto* c : it->second) {
                if (static_cast<int>(c->children.size()) < maxChildren) {
                    candidates.emplace_back(c, tierDist);
                }
            }
        }
        if (!candidates.empty()) break;
    }

    // Same tier for tier 0
    if (candidates.empty() && tierIdx == 0) {
        auto it = available.find(0);
        if (it != available.end()) {
            for (auto* c : it->second) {
                if (static_cast<int>(c->children.size()) < maxChildren &&
                    c->formId != node.formId) {
                    candidates.emplace_back(c, 0);
                }
            }
        }
    }

    // Last resort: a parent past the cap (kFallbackExtraChildren over it), never above the
    // spell's own tier - its tier first, then down to the root's. The build drops a full
    // parent from `available`, so there it finds nothing the steps above did not; the graft
    // keeps full parents in it, so a spell whose tier has no room still gets one
    for (int d = tierIdx; d >= 0 && candidates.empty(); --d) {
        const auto it = available.find(d);
        if (it == available.end()) continue;
        for (auto* p : it->second) {
            if (static_cast<int>(p->children.size()) < maxChildren + kFallbackExtraChildren && p->formId != node.formId) {
                candidates.emplace_back(p, std::abs(tierIdx - d) + 5);
            }
        }
    }

    if (candidates.empty()) return nullptr;

    // Score candidates
    std::uniform_real_distribution<float> jitter(-2.0f, 2.0f);
    float bestScore = -std::numeric_limits<float>::max();
    TreeBuilder::TreeNode* bestParent = nullptr;

    for (auto& [candidate, tierDist] : candidates) {
        float score = 0.0f;

        // Tier distance penalty
        score -= std::max(0, tierDist - 1) * 5.0f;

        // Effect-name affinity (strongest signal)
        float effectSim = sims.GetEffectSim(node.formId, candidate->formId);
        score += effectSim * 40.0f;

        // Theme match
        if (!node.theme.empty() && !candidate->theme.empty()) {
            if (SharesTheme(node, *candidate)) {
                float themeBonus = (effectSim > 0.5f) ? 25.0f : 15.0f;
                score += themeBonus;
            } else {
                score -= 10.0f;
            }
        }

        score += grouping.Score(node, *candidate);
        if (SharesModPlugin(node, *candidate)) score += kSameModBonus;

        // Combined NLP similarity
        float textSim = sims.GetTextSim(node.formId, candidate->formId);
        float nameSim = sims.GetNameSim(node.formId, candidate->formId);
        float combinedSim = textSim * 0.4f + nameSim * 0.6f;
        score += combinedSim * 30.0f;

        // Load balance
        score -= static_cast<float>(candidate->children.size()) * 8.0f;

        // Prefer immediate predecessor tier
        if (candidate->depth == tierIdx - 1) score += 10.0f;
        else if (candidate->depth == tierIdx - 2) score += 5.0f;

        // Random jitter (the build only)
        if (rng) score += jitter(*rng);

        if (parentBonus) {
            const auto bonus = parentBonus->find(candidate->formId);
            if (bonus != parentBonus->end()) score += bonus->second;
        }

        if (score > bestScore) {
            bestScore = score;
            bestParent = candidate;
        }
    }

    return bestParent;
}

TreeBuilder::BuildResult TreeBuilder::BuildClassic(
    const std::vector<json>& spells,
    const BuildConfig& config)
{
    auto startTime = std::chrono::steady_clock::now();
    BuildResult result;

    // Seed RNG
    int usedSeed = config.seed;
    if (usedSeed == 0) {
        usedSeed = static_cast<int>(
            std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    }
    std::mt19937 rng(usedSeed);

    int maxChildren = config.maxChildrenPerNode;

    // Adapt max_children based on grid hint
    if (config.gridHint) {
        auto& hint = *config.gridHint;
        if (hint.mode == "sun" && maxChildren > 2 && hint.avgPointsPerSchool < 40) {
            maxChildren = 2;
        } else if (hint.mode == "flat" && maxChildren < 4 && hint.avgPointsPerSchool > 60) {
            maxChildren = 4;
        }
    }

    // Group spells by school
    static const std::unordered_set<std::string> VALID_SCHOOLS = {
        "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"
    };

    std::unordered_map<std::string, std::vector<json>> schoolSpells;
    for (const auto& spell : spells) {
        auto school = spell.value("school", std::string(""));
        if (VALID_SCHOOLS.contains(school)) {
            schoolSpells[school].push_back(spell);
        }
    }

    // Discover themes
    auto themes = DiscoverThemesPerSchool(spells, config.topThemesPerSchool);
    themes = MergeWithHints(themes, config.topThemesPerSchool + 4);

    // Build each school's tree
    json treeData;
    treeData["version"] = "1.0";
    treeData["schools"] = json::object();

    for (auto& [schoolName, schoolSpellList] : schoolSpells) {
        if (schoolSpellList.empty()) continue;

        // Compute per-school similarity matrix (avoids wasted cross-school pairs)
        auto sims = ComputeSimilarityMatrix(schoolSpellList);

        auto schoolThemes = themes.contains(schoolName) ? themes[schoolName] : std::vector<std::string>{};

        // Group by tier: the spell's level (unknown -> Novice), or with
        // tiersByCost its Magicka cost rank. The level itself (skillLevel: XP,
        // the spell card) stays; only the tier the tree puts it in follows cost.
        std::vector<const json*> schoolSpellPointers;
        if (config.tiersByCost) {
            schoolSpellPointers.reserve(schoolSpellList.size());
            for (const auto& spell : schoolSpellList) schoolSpellPointers.push_back(&spell);
        }
        const auto costTiers = config.tiersByCost ? CostTiers(schoolSpellPointers)
                                                  : std::unordered_map<std::string, int>{};
        std::unordered_map<std::string, std::vector<json>> byTier;
        std::unordered_map<std::string, int> tierOf;  // formId -> tier index
        int tierSizes[TIER_COUNT] = {};
        for (const auto& spell : schoolSpellList) {
            const auto fid = spell.value("formId", std::string(""));
            const int tierIdx = TierOf(spell, costTiers);
            byTier[TIER_NAMES[tierIdx]].push_back(spell);
            tierOf[fid] = tierIdx;
            ++tierSizes[tierIdx];
        }
        if (config.tiersByCost) {
            logger::info("TreeBuilder: {} tiers by Magicka cost {}/{}/{}/{}/{} ({} without a cost)", schoolName,
                tierSizes[0], tierSizes[1], tierSizes[2], tierSizes[3], tierSizes[4],
                schoolSpellList.size() - costTiers.size());
        }
        const auto tierIndexOf = [&tierOf](const std::string& fid) {
            const auto it = tierOf.find(fid);
            return it != tierOf.end() ? it->second : 0;
        };

        // Pick root
        auto* rootSpell = PickRoot(byTier, config, schoolName, rng);
        if (!rootSpell) continue;

        auto rootFormId = rootSpell->value("formId", std::string(""));

        // Create all nodes
        std::unordered_map<std::string, TreeNode> nodes;
        for (const auto& spell : schoolSpellList) {
            auto node = TreeNode::FromSpell(spell);
            nodes[node.formId] = std::move(node);
        }

        auto& root = nodes[rootFormId];
        root.isRoot = true;
        root.depth = 0;

        // Assign themes
        for (auto& [fid, node] : nodes) {
            if (!schoolThemes.empty()) {
                auto [theme, score] = GetSpellPrimaryTheme(node.spellData, schoolThemes);
                node.theme = (score > 30) ? theme : "";
                node.themes = GetSpellThemes(node.spellData, schoolThemes);
            }
        }
        DropCommonThemes(nodes, config.commonThemeShare);
        const TypeGrouping grouping(nodes, config.typeGrouping);

        // Build tree tier-by-tier
        std::unordered_set<std::string> connected;
        connected.insert(rootFormId);

        std::unordered_map<int, std::vector<TreeNode*>> available;
        available[0].push_back(&root);

        for (int tierIdx = 0; tierIdx < TIER_COUNT; ++tierIdx) {
            auto tierName = std::string(TIER_NAMES[tierIdx]);
            auto it = byTier.find(tierName);
            if (it == byTier.end()) continue;

            auto tierSpells = it->second;

            // Shuffle for variety
            std::shuffle(tierSpells.begin(), tierSpells.end(), rng);

            std::vector<TreeNode*> placedThisTier;

            for (const auto& spell : tierSpells) {
                auto fid = spell.value("formId", std::string(""));
                if (fid == rootFormId || connected.contains(fid)) continue;

                auto nodeIt = nodes.find(fid);
                if (nodeIt == nodes.end()) continue;

                auto* parent = FindBestClassicParent(
                    nodeIt->second, available, tierIdx, maxChildren, sims, grouping, &rng);

                if (parent) {
                    LinkNodes(*parent, nodeIt->second);
                    nodeIt->second.depth = tierIdx;
                    connected.insert(fid);
                    placedThisTier.push_back(&nodeIt->second);

                    // Update availability
                    if (static_cast<int>(parent->children.size()) >= maxChildren) {
                        for (auto& [d, plist] : available) {
                            plist.erase(
                                std::remove_if(plist.begin(), plist.end(),
                                    [&](TreeNode* p) { return p->formId == parent->formId; }),
                                plist.end());
                        }
                    }
                }
            }

            // Add placed nodes as available parents
            for (auto* pn : placedThisTier) {
                if (static_cast<int>(pn->children.size()) < maxChildren) {
                    available[tierIdx].push_back(pn);
                }
            }
        }

        // Force-connect unconnected nodes
        std::vector<std::string> unconnectedIds;
        for (const auto& [nid, nd] : nodes) {
            if (!connected.contains(nid)) {
                unconnectedIds.push_back(nid);
            }
        }

        for (const auto& orphanId : unconnectedIds) {
            auto& orphanNode = nodes[orphanId];
            int nodeTierIdx = tierIndexOf(orphanId);
            TreeNode* bestParent = nullptr;
            float bestScore = -std::numeric_limits<float>::max();

            for (const auto& cid : connected) {
                auto& cnode = nodes[cid];
                int cnodeTierIdx = tierIndexOf(cid);
                float score = 0.0f;

                if (cnodeTierIdx <= nodeTierIdx) {
                    score += 100.0f;
                    score -= static_cast<float>(nodeTierIdx - cnodeTierIdx) * 5.0f;
                } else {
                    score -= 200.0f;
                }

                float effectSim = sims.GetEffectSim(orphanId, cid);
                score += effectSim * 30.0f;

                if (SharesTheme(orphanNode, cnode)) {
                    score += 15.0f;
                }
                score += grouping.Score(orphanNode, cnode);
                if (SharesModPlugin(orphanNode, cnode)) score += kSameModBonus;

                score -= static_cast<float>(cnode.children.size()) * 8.0f;

                if (score > bestScore) {
                    bestScore = score;
                    bestParent = &cnode;
                }
            }

            if (bestParent) {
                LinkNodes(*bestParent, orphanNode);
                orphanNode.depth = nodeTierIdx;
                connected.insert(orphanId);
            }
        }

        // Serialize nodes
        json schoolResult;
        schoolResult["root"] = rootFormId;
        schoolResult["layoutStyle"] = "tier_first";

        json nodesList = json::array();
        for (const auto& [nid, nd] : nodes) {
            nodesList.push_back(nd.ToDict());
        }
        schoolResult["nodes"] = nodesList;
        schoolResult["config_used"] = {
            {"shape", "tier_first"},
            {"density", config.density},
            {"symmetry", config.symmetry},
            {"source", "classic"}
        };

        treeData["schools"][schoolName] = schoolResult;
    }

    // Metadata
    treeData["generatedAt"] = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    treeData["generator"] = "ClassicTreeBuilder (Tier-First, C++)";
    treeData["seed"] = usedSeed;
    treeData["config"] = {
        {"shape", "tier_first"},
        {"density", config.density},
        {"symmetry", config.symmetry},
        {"type_grouping", config.typeGrouping},
        {"tier_mode", config.tiersByCost ? "cost" : "level"}
    };

    // Validate and auto-fix
    ValidateAndFix(treeData, maxChildren, config.autoFixUnreachable);

    auto endTime = std::chrono::steady_clock::now();
    result.elapsedMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    result.treeData = std::move(treeData);
    result.success = true;
    return result;
}
