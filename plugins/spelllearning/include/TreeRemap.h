#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// TREE REMAP - a saved tree's formIds after a load order change
// =============================================================================
//
// SpellScanner::ValidateAndFixTree asks the game about every node (its
// persistentId resolved, still a player's spell); this does the rest with no
// game types, so tools/tree-remap-check runs it on made-up trees.
//
// The rule it keeps: every link in a saved file (children, prerequisites,
// hardPrereqs, softPrereqs, locks' nodeId, bridges' ends, a school's root)
// holds the formId its node had when the file was saved. So each link is
// looked up once, by that saved formId: saved -> final for a node that moved,
// dropped for a node that went. A final formId that equals another node's saved
// one (light plugin A removed, B and C one index down; or two plugins swapping
// indices) is never looked up again, so no link lands on the wrong spell.
//
// A node without a persistentId (a tree from before they were saved) keeps its
// saved formId when that is still a player's spell. When a node with a
// persistentId now has that same formId, it was the other spell's slot: the
// persistentId is the record, the legacy node goes (and the links to it).
//
// O(nodes + links) time and space.
namespace TreeRemap
{
    using Json = nlohmann::json;

    /// What the game says about one saved node (SpellScannerFormId.cpp asks it; a check makes it up)
    struct NodeVerdict
    {
        bool keep = false;              ///< still a player's spell in this load order
        std::string formId;             ///< its formId now (kept nodes)
        bool fromPersistentId = false;  ///< formId came from the node's persistentId (it may be the saved one)
        std::string missingPlugin;      ///< a persistentId that did not resolve: its plugin, for the report
    };

    struct Report
    {
        int totalNodes = 0;
        int validNodes = 0;
        int invalidNodes = 0;
        int resolvedFromPersistent = 0;  ///< kept nodes whose formId changed
        int legacyCollisions = 0;        ///< legacy nodes dropped for a persistentId node on their formId
        std::size_t bridgesDropped = 0;
        std::set<std::string> missingPlugins;
        std::set<std::string> removedFormIds;                     ///< saved formIds whose links go
        std::unordered_map<std::string, std::string> moved;       ///< saved formId -> final formId
        std::vector<std::string> collisions;                      ///< saved formIds of the dropped legacy nodes
    };

    /// Every list of formIds a node holds (C++ reads hardPrereqs / softPrereqs; the panel all four)
    inline constexpr std::array<const char*, 4> kLinkLists = { "children", "prerequisites", "hardPrereqs", "softPrereqs" };

    namespace Detail
    {
        struct NodeEntry
        {
            Json* schoolNodes = nullptr;
            std::size_t index = 0;
            std::string saved;  ///< empty: the node had no string formId
            NodeVerdict verdict;
        };

        /// The final formId of a saved link, or empty when its node went; a formId no node had stays
        [[nodiscard]] inline std::string MapLink(const std::string& saved, const Report& report)
        {
            if (report.removedFormIds.contains(saved)) return {};
            const auto it = report.moved.find(saved);
            return it != report.moved.end() ? it->second : saved;
        }

        /// One list of formId strings, each looked up once by its saved value
        inline void MapList(Json& links, const Report& report)
        {
            Json out = Json::array();
            for (auto& link : links) {
                if (!link.is_string()) {
                    out.push_back(std::move(link));
                    continue;
                }
                auto mapped = MapLink(link.get<std::string>(), report);
                if (!mapped.empty()) out.push_back(std::move(mapped));
            }
            links = std::move(out);
        }

        [[nodiscard]] inline bool HasNodes(const Json& school)
        {
            return school.is_object() && school.contains("nodes") && school["nodes"].is_array();
        }

        /// Pass 1: a verdict for every node, nothing changed yet
        template <class Judge>
        std::vector<NodeEntry> JudgeAll(Json& schools, Judge& judge, Report& report)
        {
            std::vector<NodeEntry> entries;
            for (auto& [schoolName, school] : schools.items()) {
                if (!HasNodes(school)) continue;
                auto& nodes = school["nodes"];
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    NodeEntry entry{ &nodes, i, {}, {} };
                    report.totalNodes++;
                    const auto& node = nodes[i];
                    if (node.is_object() && node.contains("formId") && node["formId"].is_string()) {
                        entry.saved = node["formId"].get<std::string>();
                        entry.verdict = judge(node);
                    }
                    entries.push_back(std::move(entry));
                }
            }
            return entries;
        }

        /// A legacy node kept on the formId a persistentId node (saved under another formId) now has goes
        inline void DropLegacyCollisions(std::vector<NodeEntry>& entries, Report& report)
        {
            std::unordered_map<std::string, std::string> persistentOwner;  // final formId -> saved formId
            for (const auto& e : entries) {
                if (e.verdict.keep && e.verdict.fromPersistentId) persistentOwner.emplace(e.verdict.formId, e.saved);
            }
            for (auto& e : entries) {
                if (!e.verdict.keep || e.verdict.fromPersistentId) continue;
                const auto owner = persistentOwner.find(e.verdict.formId);
                if (owner == persistentOwner.end() || owner->second == e.saved) continue;
                e.verdict.keep = false;
                report.legacyCollisions++;
                report.collisions.push_back(e.saved);
            }
        }

        /// The saved -> final map and the saved formIds whose links go
        inline void MakeMaps(const std::vector<NodeEntry>& entries, Report& report)
        {
            std::unordered_set<std::string> keptSaved;
            std::set<std::string> goneSaved;
            for (const auto& e : entries) {
                if (e.verdict.keep) {
                    keptSaved.insert(e.saved);
                    if (e.verdict.formId != e.saved) {
                        report.moved[e.saved] = e.verdict.formId;
                        report.resolvedFromPersistent++;
                    }
                } else if (!e.saved.empty()) {
                    goneSaved.insert(e.saved);
                }
                if (!e.verdict.missingPlugin.empty()) report.missingPlugins.insert(e.verdict.missingPlugin);
            }
            // A saved formId some kept node still answers to (the same spell in two places) keeps its links
            for (const auto& id : goneSaved) {
                if (!keptSaved.contains(id)) report.removedFormIds.insert(id);
            }
        }

        /// Kept nodes take their final formId, the rest leave their school
        inline void ApplyNodes(const std::vector<NodeEntry>& entries, Report& report)
        {
            std::unordered_map<Json*, std::vector<std::size_t>> drop;
            for (const auto& e : entries) {
                if (e.verdict.keep) {
                    (*e.schoolNodes)[e.index]["formId"] = e.verdict.formId;
                    report.validNodes++;
                } else {
                    drop[e.schoolNodes].push_back(e.index);
                    report.invalidNodes++;
                }
            }
            for (auto& [nodes, indices] : drop) {
                for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
                    nodes->erase(nodes->begin() + static_cast<std::ptrdiff_t>(*it));
                }
            }
        }

        inline void ApplyLinks(Json& schools, Report& report)
        {
            for (auto& [schoolName, school] : schools.items()) {
                if (!HasNodes(school)) continue;
                auto& nodes = school["nodes"];
                for (auto& node : nodes) {
                    if (!node.is_object()) continue;
                    for (const char* key : kLinkLists) {
                        if (node.contains(key) && node[key].is_array()) MapList(node[key], report);
                    }
                    // Locks ({nodeId, ...}, Pre Req Master) name the node they wait on
                    if (node.contains("locks") && node["locks"].is_array()) {
                        Json kept = Json::array();
                        for (auto& lock : node["locks"]) {
                            if (lock.is_object() && lock.contains("nodeId") && lock["nodeId"].is_string()) {
                                auto mapped = MapLink(lock["nodeId"].get<std::string>(), report);
                                if (mapped.empty()) continue;
                                lock["nodeId"] = std::move(mapped);
                            }
                            kept.push_back(std::move(lock));
                        }
                        node["locks"] = std::move(kept);
                    }
                }
                // A root that went: the first spell left; one that moved: its new formId
                if (school.contains("root") && school["root"].is_string()) {
                    auto mapped = MapLink(school["root"].get<std::string>(), report);
                    if (!mapped.empty()) {
                        school["root"] = std::move(mapped);
                    } else if (!nodes.empty() && nodes[0].is_object() && nodes[0].contains("formId")) {
                        school["root"] = nodes[0]["formId"];
                    }
                }
            }
        }

        /// Cross school bridges ({from, to}): an end that moved follows, a bridge with an end gone goes
        inline void ApplyBridges(Json& tree, Report& report)
        {
            if (!tree.contains("bridges") || !tree["bridges"].is_array()) return;
            auto& bridges = tree["bridges"];
            Json kept = Json::array();
            for (auto& bridge : bridges) {
                bool whole = bridge.is_object();
                for (const char* end : { "from", "to" }) {
                    if (!whole) break;
                    if (!bridge.contains(end) || !bridge[end].is_string()) {
                        whole = false;
                        break;
                    }
                    auto mapped = MapLink(bridge[end].get<std::string>(), report);
                    if (mapped.empty()) {
                        whole = false;
                        break;
                    }
                    bridge[end] = std::move(mapped);
                }
                if (whole) kept.push_back(std::move(bridge));
            }
            report.bridgesDropped = bridges.size() - kept.size();
            bridges = std::move(kept);
        }
    }

    /**
     * Fixes a saved tree in place for the current load order.
     * @param judge  NodeVerdict(const Json& node), called once for each node with a string formId
     */
    template <class Judge>
    Report FixTree(Json& tree, Judge&& judge)
    {
        Report report;
        if (!tree.is_object() || !tree.contains("schools") || !tree["schools"].is_object()) return report;
        auto& schools = tree["schools"];
        auto entries = Detail::JudgeAll(schools, judge, report);
        Detail::DropLegacyCollisions(entries, report);
        Detail::MakeMaps(entries, report);
        Detail::ApplyNodes(entries, report);
        Detail::ApplyLinks(schools, report);
        Detail::ApplyBridges(tree, report);
        return report;
    }
}
