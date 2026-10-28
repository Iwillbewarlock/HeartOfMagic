#pragma once

#include "FormIdText.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <string_view>
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
// FormIds are compared in one form, "0x%08X" (NormalizeFormId).
//
// A node without a persistentId (a tree from before they were saved):
// - saved under the same formId as a node with one (the same spell in another
//   school), it follows that node: its formId, or out when that node went;
// - otherwise it keeps its saved formId when that is still a player's spell,
//   unless a node with a persistentId now has that formId: it was that spell's
//   slot, the persistentId is the record, and the legacy node goes.
//
// A node whose persistentId named nothing but was found again by editor id
// (EditorIdRematch.h: its plugin renamed, merged or compacted) takes that spell's
// formId and persistentId. When another node of its school already has that spell
// (it resolved by its own persistentId, or an earlier rematch took it), the
// rematched node goes: one spell is one node in a school.
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
        bool hasPersistentId = false;   ///< the node has a well-formed persistentId (its formId came from it)
        std::string missingPlugin;      ///< the persistentId's plugin is not loaded: that plugin
        std::string missingRecord;      ///< the plugin is loaded but has no such record: the persistentId
        bool rematched = false;         ///< its persistentId named nothing; found again by editor id
        std::string persistentId;       ///< a rematched node's new persistentId (the found spell's)
    };

    struct RootChange
    {
        std::string school;
        std::string from;
        std::string to;
    };

    struct Report
    {
        bool hasSchools = false;         ///< "schools" was an object (nothing is done otherwise)
        int totalNodes = 0;
        int validNodes = 0;
        int invalidNodes = 0;
        int nodesWithoutFormId = 0;      ///< not an object, or no string formId (always dropped)
        int resolvedFromPersistent = 0;  ///< kept nodes whose formId changed, rematched ones not counted
        int legacyFollowed = 0;          ///< legacy nodes that took a persistentId node's verdict (same saved formId)
        int legacyCollisions = 0;        ///< legacy nodes dropped for a persistentId node on their formId
        int rematched = 0;               ///< kept nodes found again by editor id (each takes a new persistentId)
        std::size_t bridgesDropped = 0;
        std::set<std::string> missingPlugins;                ///< plugins persistentIds name that are not loaded
        std::set<std::string> missingRecords;                ///< persistentIds of loaded plugins without the record
        std::set<std::string> removedFormIds;                ///< saved formIds whose links go
        std::unordered_map<std::string, std::string> moved;  ///< saved formId -> final formId
        std::vector<std::string> collisions;                 ///< saved formIds of the dropped legacy nodes
        std::vector<std::string> rematchClashes;             ///< saved formIds of rematched nodes on a taken spell
        std::vector<RootChange> rootChanges;

        /// The fixed tree differs from the saved one: a node moved, went or took a new
        /// persistentId, or a root or bridge changed (the panel must get the fixed tree)
        [[nodiscard]] bool Changed() const
        {
            return !moved.empty() || rematched > 0 || invalidNodes > 0 || bridgesDropped > 0 || !rootChanges.empty();
        }
    };

    /// Every list of formIds a node holds (C++ reads hardPrereqs / softPrereqs; the panel all four)
    inline constexpr std::array<const char*, 4> kLinkLists = { "children", "prerequisites", "hardPrereqs", "softPrereqs" };

    /// "0x%08X" for a hex formId ("0x" optional, any case); any other text as it is
    [[nodiscard]] inline std::string NormalizeFormId(std::string_view text)
    {
        const auto value = FormIdText::Parse(text);
        return value ? FormIdText::Format(*value) : std::string(text);
    }

    namespace Detail
    {
        struct NodeEntry
        {
            Json* schoolNodes = nullptr;
            std::size_t index = 0;
            std::string saved;  ///< normalised; empty: the node had no string formId
            NodeVerdict verdict;
            bool clashDropped = false;  ///< rematched, then dropped for a spell its school has (DropRematchClashes)
        };

        /// The final formId of a saved link, or empty when its node went; a formId no node had stays
        [[nodiscard]] inline std::string MapLink(const std::string& saved, const Report& report)
        {
            auto id = NormalizeFormId(saved);
            if (report.removedFormIds.contains(id)) return {};
            const auto it = report.moved.find(id);
            return it != report.moved.end() ? it->second : id;
        }

        /// One list of formId strings, each looked up once by its saved value; other values stay
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
                        entry.saved = NormalizeFormId(node["formId"].get<std::string>());
                        entry.verdict = judge(node, schoolName);
                        entry.verdict.formId = NormalizeFormId(entry.verdict.formId);
                    } else {
                        report.nodesWithoutFormId++;
                    }
                    entries.push_back(std::move(entry));
                }
            }
            return entries;
        }

        /// A rematched node on a spell another node of its school has goes (see the top): the nodes
        /// that resolved by their own persistentId first, then the rematched ones in file order
        inline void DropRematchClashes(std::vector<NodeEntry>& entries, Report& report)
        {
            std::unordered_map<const Json*, std::unordered_set<std::string>> taken;  // school -> final formIds
            for (const auto& e : entries) {
                if (e.verdict.keep && e.verdict.hasPersistentId && !e.verdict.rematched) {
                    taken[e.schoolNodes].insert(e.verdict.formId);
                }
            }
            for (auto& e : entries) {
                if (!e.verdict.keep || !e.verdict.rematched) continue;
                if (taken[e.schoolNodes].insert(e.verdict.formId).second) continue;
                e.verdict.keep = false;
                e.clashDropped = true;
                report.rematchClashes.push_back(e.saved);
            }
        }

        /// The legacy rules (see the top): follow a persistentId node saved under the same formId,
        /// or go when a persistentId node saved under another formId now has this one. A copy in
        /// another school follows a node dropped by DropRematchClashes as it was before the clash,
        /// which concerned that node's school only
        inline void ReconcileLegacy(std::vector<NodeEntry>& entries, Report& report)
        {
            std::unordered_map<std::string, const NodeEntry*> persistentBySaved;
            std::unordered_map<std::string, std::string> persistentOwner;  // final formId -> saved formId
            for (const auto& e : entries) {
                if (e.saved.empty() || !e.verdict.hasPersistentId) continue;
                persistentBySaved.emplace(e.saved, &e);
                if (e.verdict.keep) persistentOwner.emplace(e.verdict.formId, e.saved);
            }
            for (auto& e : entries) {
                if (e.saved.empty() || e.verdict.hasPersistentId) continue;
                if (const auto same = persistentBySaved.find(e.saved); same != persistentBySaved.end()) {
                    const NodeEntry& source = *same->second;
                    e.verdict.keep = source.verdict.keep || (source.clashDropped && source.schoolNodes != e.schoolNodes);
                    e.verdict.formId = source.verdict.formId;
                    report.legacyFollowed++;
                    continue;
                }
                if (!e.verdict.keep) continue;
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
                    if (e.verdict.rematched) report.rematched++;
                    if (e.verdict.formId != e.saved) {
                        report.moved[e.saved] = e.verdict.formId;
                        if (!e.verdict.rematched) report.resolvedFromPersistent++;
                    }
                } else if (!e.saved.empty()) {
                    goneSaved.insert(e.saved);
                }
                if (!e.verdict.missingPlugin.empty()) report.missingPlugins.insert(e.verdict.missingPlugin);
                if (!e.verdict.missingRecord.empty()) report.missingRecords.insert(e.verdict.missingRecord);
            }
            // A saved formId some kept node still answers to (the same spell in two places) keeps its links
            for (const auto& id : goneSaved) {
                if (!keptSaved.contains(id)) report.removedFormIds.insert(id);
            }
        }

        /// Kept nodes take their final formId (a rematched one its new persistentId too), the rest leave their school
        inline void ApplyNodes(const std::vector<NodeEntry>& entries, Report& report)
        {
            std::unordered_map<Json*, std::vector<std::size_t>> drop;
            for (const auto& e : entries) {
                if (e.verdict.keep) {
                    auto& node = (*e.schoolNodes)[e.index];
                    node["formId"] = e.verdict.formId;
                    if (e.verdict.rematched && !e.verdict.persistentId.empty()) {
                        node["persistentId"] = e.verdict.persistentId;
                    }
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

        /// Locks ({nodeId, ...}, Pre Req Master) name the node they wait on; a lock without one stays
        inline void MapLocks(Json& locks, const Report& report)
        {
            Json kept = Json::array();
            for (auto& lock : locks) {
                if (lock.is_object() && lock.contains("nodeId") && lock["nodeId"].is_string()) {
                    auto mapped = MapLink(lock["nodeId"].get<std::string>(), report);
                    if (mapped.empty()) continue;
                    lock["nodeId"] = std::move(mapped);
                }
                kept.push_back(std::move(lock));
            }
            locks = std::move(kept);
        }

        /// A root that went: the first spell left; one that moved: its new formId; one no node had stays
        inline void MapRoot(const std::string& schoolName, Json& school, Report& report)
        {
            if (!school.contains("root") || !school["root"].is_string()) return;
            const auto& nodes = school["nodes"];
            const auto was = school["root"].get<std::string>();
            auto now = MapLink(was, report);
            if (now.empty()) {
                if (nodes.empty() || !nodes[0].is_object() || !nodes[0].contains("formId")) return;
                now = nodes[0]["formId"].get<std::string>();
            }
            if (now == was) return;
            school["root"] = now;
            report.rootChanges.push_back({ schoolName, was, std::move(now) });
        }

        inline void ApplyLinks(Json& schools, Report& report)
        {
            for (auto& [schoolName, school] : schools.items()) {
                if (!HasNodes(school)) continue;
                for (auto& node : school["nodes"]) {
                    if (!node.is_object()) continue;
                    for (const char* key : kLinkLists) {
                        if (node.contains(key) && node[key].is_array()) MapList(node[key], report);
                    }
                    if (node.contains("locks") && node["locks"].is_array()) MapLocks(node["locks"], report);
                }
                MapRoot(schoolName, school, report);
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
     * @param judge  NodeVerdict(const Json& node, const std::string& school), called once for each node with
     *               a string formId, with the name of the school it is saved under
     */
    template <class Judge>
    Report FixTree(Json& tree, Judge&& judge)
    {
        Report report;
        if (!tree.is_object() || !tree.contains("schools") || !tree["schools"].is_object()) return report;
        report.hasSchools = true;
        auto& schools = tree["schools"];
        auto entries = Detail::JudgeAll(schools, judge, report);
        Detail::DropRematchClashes(entries, report);
        Detail::ReconcileLegacy(entries, report);
        Detail::MakeMaps(entries, report);
        Detail::ApplyNodes(entries, report);
        Detail::ApplyLinks(schools, report);
        Detail::ApplyBridges(tree, report);
        return report;
    }
}
