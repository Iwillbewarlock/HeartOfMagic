#pragma once

#include "ScanRecordRules.h"
#include "TreeRemap.h"
#include "treebuilder/GraftCandidates.h"
#include "treebuilder/LayoutDeclutter.h"
#include "treebuilder/TreeGraft.h"
#include "uimanager/TreeGraftChecks.h"
#include "uimanager/TreeGraftRecord.h"
#include "uimanager/TreeRecord.h"

#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// TREE GRAFT RUN - the graft at the main menu, its rules without the game
// (TreeGraftRun.cpp wires them; tools/graft-run-check.cpp runs them offline;
// docs/superpowers/specs/2026-10-28-tree-graft-design.md, sections 1 and 4)
// =============================================================================
//
// When: the tree record's load order is not the current one (an empty one is
// none recorded), the catalog is the current load order's full scan
// (ScanRecord::CatalogMatches: a scan made before the mod came does not count),
// and that scan marked everything with the tome reach pass ready.
//
// Seen (BaselineOf): the record's list; without one (a tree from before it, or
// imported since) the spells of the catalog the scan at game start replaced;
// without that either, none - the runner grafts nothing and writes nothing.
//
// The plan (PlanGraft), on the file's own tree:
//  1. Ids refreshed (RefreshIds): a live node's formId becomes what validation
//     says it is now (TreeGraft.h's precondition), and every link to it follows,
//     in every node; a rematched node takes its new persistentId. A node
//     validation drops (a removed plugin's, one no longer a player's spell) keeps
//     its id, and a live node whose new formId is such a node's saved one is held
//     at its old id (two nodes on one formId would link wrongly at the next load);
//     a new spell on that old id is then deferred, as Graft does.
//  2. Graft (TreeGraft.h) over the scan less the spells only dropped nodes hold,
//     so a node the tree load drops is never a parent.
//  3. The declutter with every existing node fixed and no spread: only the
//     grafted nodes move.
//  4. Checked (TreeGraftChecks.h; the dev self-test reports it): every pre-graft node as it was,
//     node by node in file order, but for its ids (formId and persistentId,
//     and the links' ids, through the id map its own formId change makes:
//     Ruling 2 counts an id rewrite as no change) and the grafted children a
//     parent gained; no seen spell grafted. A plan that fails is not written.
//  5. The record: seen += grafted + left out by the player's rules (not the
//     unplaced or deferred ones: a later run judges them again), pendingGrafted
//     += grafted; the load order becomes the current one only when nothing was
//     deferred or unplaced, so the graft runs again at the next start and the
//     "rebuild" notice still applies to those spells.
namespace TreeGraftRun
{
    using Json = nlohmann::json;
    using OrderedJson = nlohmann::ordered_json;

    /// The field the validated copy carries per node, back to the file's node
    inline constexpr const char* kNodeTag = "graftNodeTag";
    inline constexpr const char* kTomeReachReady = "ready";   // TomeReach::OutcomeKey of a pass that answered

    // -------------------------------------------------------------------------
    // When it runs
    // -------------------------------------------------------------------------

    struct Facts
    {
        std::string loadOrder;         ///< LoadOrderFingerprint hash now
        std::string recordLoadOrder;   ///< spell_tree_loadorder.json "loadOrder" (empty: none)
        bool recordUnreadable = false; ///< the record is there but not a JSON object
        bool treeHasSpells = false;
        bool catalogMatches = false;   ///< ScanRecord::CatalogMatches(now)
    };

    /// Why the graft does not run, or nothing: it runs (the scan aside, WhyScanUnusable)
    [[nodiscard]] inline std::optional<std::string> WhyNotRun(const Facts& facts)
    {
        if (facts.loadOrder.empty()) return std::string("the load order is unknown");
        if (facts.recordUnreadable) return std::string("spell_tree_loadorder.json could not be read");
        if (facts.recordLoadOrder == facts.loadOrder) return std::string("the tree's record is this load order's");
        if (!facts.treeHasSpells) return std::string("no saved tree with spells");
        if (!facts.catalogMatches) return std::string("the catalog is not this load order's full scan");
        return std::nullopt;
    }

    /// AutoScan's skip (the catalog is this load order's) when a graft is due but
    /// spell_scan_output.json is not that scan's dump (a panel-only full scan since, or a
    /// crash between the catalog and the dump): the graft would stop on it at every start,
    /// so the scan runs instead and writes one (Ruling 22). `dumpMatches()` reads and hashes
    /// the dump: asked only when the graft is due.
    template <class DumpMatches>
    [[nodiscard]] bool ScanForGraft(const Facts& facts, DumpMatches&& dumpMatches)
    {
        return !WhyNotRun(facts) && !dumpMatches();
    }

    /// Why the scan output cannot be grafted from, or nothing
    [[nodiscard]] inline std::optional<std::string> WhyScanUnusable(const Json& output, const std::string& tomeReach)
    {
        if (!output.is_object() || !output.contains("spells") || !output["spells"].is_array()) {
            return std::string("spell_scan_output.json has no spells list");
        }
        if (const auto incomplete = ScanRecordRules::IncompleteOf(output); !incomplete.empty()) {
            return "the scan did not mark everything (" + incomplete + ")";
        }
        if (tomeReach != kTomeReachReady) return "the tome reach pass is not ready (" + tomeReach + ")";
        return std::nullopt;
    }

    // -------------------------------------------------------------------------
    // Validation's verdicts and the id refresh
    // -------------------------------------------------------------------------

    namespace Detail
    {
        /// Each node validation judges (an object with a string formId, in a school with a
        /// nodes list), in the tree's own order: fn(node, school, index)
        template <class TreeJson, class Fn>
        void ForEachJudgedNode(TreeJson& tree, Fn&& fn)
        {
            if (!tree.is_object()) return;
            const auto schools = tree.find("schools");
            if (schools == tree.end() || !schools->is_object()) return;
            for (auto& [name, school] : schools->items()) {
                if (!school.is_object()) continue;
                const auto nodes = school.find("nodes");
                if (nodes == school.end() || !nodes->is_array()) continue;
                for (std::size_t i = 0; i < nodes->size(); ++i) {
                    auto& node = (*nodes)[i];
                    if (node.is_object() && node.contains("formId") && node["formId"].is_string()) fn(node, name, i);
                }
            }
        }

        template <class J>
        [[nodiscard]] std::string Text(const J& object, const char* key)
        {
            return TreeGraft::Detail::Text(object, key);
        }
    }

    /// What validation left of one node: kept, with its formId and persistentId then
    struct NodeVerdict
    {
        bool kept = false;
        std::string formId;
        std::string persistentId;
    };

    /// A copy for SpellScanner::ValidateAndFixTree (game thread), each judged node tagged
    /// with its place in the file's order; `tagged` = how many
    [[nodiscard]] inline Json TaggedCopy(const OrderedJson& tree, std::size_t& tagged)
    {
        OrderedJson copy = tree;
        tagged = 0;
        Detail::ForEachJudgedNode(copy, [&](OrderedJson& node, const std::string&, std::size_t) { node[kNodeTag] = tagged++; });
        return Json(copy);
    }

    /// The verdicts out of the validated copy: a tagged node still there was kept
    [[nodiscard]] inline std::vector<NodeVerdict> VerdictsOf(const Json& validated, std::size_t tagged)
    {
        std::vector<NodeVerdict> verdicts(tagged);
        Detail::ForEachJudgedNode(validated, [&](const Json& node, const std::string&, std::size_t) {
            const auto tag = node.find(kNodeTag);
            if (tag == node.end() || !tag->is_number_unsigned()) return;
            const auto at = tag->get<std::size_t>();
            if (at < tagged) verdicts[at] = { true, Detail::Text(node, "formId"), Detail::Text(node, "persistentId") };
        });
        return verdicts;
    }

    /// The nodes validation found again by editor id: old persistentId (A-Z folded) -> the
    /// found spell's (both well-formed and different), for the check (PreGraftNodesKept)
    [[nodiscard]] inline std::map<std::string, std::string> RematchedIds(const OrderedJson& tree,
        const std::vector<NodeVerdict>& verdicts)
    {
        std::map<std::string, std::string> rematched;
        std::size_t at = 0;
        Detail::ForEachJudgedNode(tree, [&](const OrderedJson& node, const std::string&, std::size_t) {
            if (at >= verdicts.size()) return;
            const auto& verdict = verdicts[at++];
            const auto was = Detail::Text(node, "persistentId");
            if (verdict.kept && was.find('|') != std::string::npos &&
                verdict.persistentId.find('|') != std::string::npos && verdict.persistentId != was) {
                rematched[AsciiText::ToLower(was)] = verdict.persistentId;
            }
        });
        return rematched;
    }

    struct Refresh
    {
        std::map<std::string, std::string> moved;   ///< saved formId -> its formId now ("0x%08X")
        std::size_t nodesMoved = 0, persistentIdsWritten = 0;
        std::vector<std::string> held;              ///< saved formIds left as they were (see the top)
    };

    /**
     * Stale ids of live nodes refreshed in the file's tree (see the top, step 1); each
     * link is looked up once by the id it holds, as TreeRemap does. Throws when the
     * verdicts are not this tree's (their count), before anything is changed.
     * O(nodes + links) per round; a round per held move at most (a held move frees no id).
     */
    inline Refresh RefreshIds(OrderedJson& tree, const std::vector<NodeVerdict>& verdicts)
    {
        struct Entry
        {
            OrderedJson* node;
            std::string saved;
            std::string now;   // kept: its formId now; dropped: empty
        };
        std::vector<Entry> entries;
        Detail::ForEachJudgedNode(tree, [&](OrderedJson& node, const std::string&, std::size_t) {
            entries.push_back({ &node, TreeRemap::NormalizeFormId(node["formId"].get<std::string>()), {} });
        });
        if (entries.size() != verdicts.size()) {
            throw std::runtime_error(std::format("{} verdicts for the tree's {} nodes with a formId", verdicts.size(), entries.size()));
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (verdicts[i].kept) entries[i].now = TreeRemap::NormalizeFormId(verdicts[i].formId);
        }

        Refresh refresh;
        auto& moves = refresh.moved;
        std::set<std::string> split;   // nodes saved under one id that validation sends to two
        for (const auto& e : entries) {
            if (e.now.empty() || e.now == e.saved) continue;
            const auto [it, added] = moves.emplace(e.saved, e.now);
            if (!added && it->second != e.now) split.insert(e.saved);
        }
        for (const auto& saved : split) {
            moves.erase(saved);
            refresh.held.push_back(saved);
        }
        const auto rewritten = [&moves](const Entry& e) {
            const auto it = moves.find(e.saved);
            return !e.now.empty() && it != moves.end() && it->second == e.now;
        };
        // A move onto an id a node keeps is held; holding it keeps that node's id too
        for (bool changed = true; changed;) {
            changed = false;
            std::unordered_set<std::string> staying;
            for (const auto& e : entries) {
                if (!rewritten(e)) staying.insert(e.saved);
            }
            for (auto it = moves.begin(); it != moves.end();) {
                if (!staying.contains(it->second)) {
                    ++it;
                    continue;
                }
                refresh.held.push_back(it->first);
                it = moves.erase(it);
                changed = true;
            }
        }

        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto& node = *entries[i].node;
            if (rewritten(entries[i])) {
                node["formId"] = entries[i].now;
                ++refresh.nodesMoved;
            }
            // A rematch (Task 6): the found spell's persistentId, whether or not its formId moved
            const auto& now = verdicts[i].persistentId;
            const auto was = Detail::Text(node, "persistentId");
            if (verdicts[i].kept && now.find('|') != std::string::npos && was.find('|') != std::string::npos && was != now) {
                node["persistentId"] = now;
                ++refresh.persistentIdsWritten;
            }
        }
        if (moves.empty()) return refresh;

        const auto map = [&moves](OrderedJson& link) {
            if (!link.is_string()) return;
            const auto it = moves.find(TreeRemap::NormalizeFormId(link.get<std::string>()));
            if (it != moves.end()) link = it->second;
        };
        auto& schools = tree["schools"];
        for (auto& [name, school] : schools.items()) {
            if (!school.is_object()) continue;
            if (school.contains("root")) map(school["root"]);
            if (!school.contains("nodes") || !school["nodes"].is_array()) continue;
            for (auto& node : school["nodes"]) {
                if (!node.is_object()) continue;
                for (const char* key : TreeRemap::kLinkLists) {
                    if (!node.contains(key) || !node[key].is_array()) continue;
                    for (auto& link : node[key]) map(link);
                }
                if (node.contains("locks") && node["locks"].is_array()) {
                    for (auto& lock : node["locks"]) {
                        if (lock.is_object() && lock.contains("nodeId")) map(lock["nodeId"]);
                    }
                }
            }
        }
        if (tree.contains("bridges") && tree["bridges"].is_array()) {
            for (auto& bridge : tree["bridges"]) {
                if (!bridge.is_object()) continue;
                for (const char* end : { "from", "to" }) {
                    if (bridge.contains(end)) map(bridge[end]);
                }
            }
        }
        return refresh;
    }

    /// The scan Graft sees: less the spells only nodes validation dropped hold (by
    /// persistentId, or by formId for a node without one), so such a node is never a
    /// live parent; `left` = how many. The spells are moved out of `scanSpells`.
    [[nodiscard]] inline std::vector<Json> LiveScanForGraft(Json scanSpells, const OrderedJson& tree,
        const std::vector<NodeVerdict>& verdicts, std::size_t& left)
    {
        using TreeGraft::Detail::PersistentKey;
        using TreeGraft::Detail::SpellIdOf;
        std::unordered_set<std::string> keptKeys, droppedKeys, keptIds, droppedIds;
        std::size_t at = 0;
        Detail::ForEachJudgedNode(tree, [&](const OrderedJson& node, const std::string&, std::size_t) {
            if (at >= verdicts.size()) return;
            const auto& verdict = verdicts[at++];
            const auto key = PersistentKey(node);
            const auto id = SpellIdOf(verdict.kept ? verdict.formId : Detail::Text(node, "formId"));
            if (!key.empty()) (verdict.kept ? keptKeys : droppedKeys).insert(key);
            else if (!id.empty()) (verdict.kept ? keptIds : droppedIds).insert(id);
        });
        left = 0;
        std::vector<Json> out;
        if (!scanSpells.is_array()) return out;
        out.reserve(scanSpells.size());
        for (auto& spell : scanSpells) {
            if (!spell.is_object()) continue;
            const auto key = PersistentKey(spell);
            const auto id = SpellIdOf(Detail::Text(spell, "formId"));
            const bool heldByKept = (!key.empty() && keptKeys.contains(key)) || keptIds.contains(id);
            const bool heldByDropped = (!key.empty() && droppedKeys.contains(key)) || droppedIds.contains(id);
            if (heldByDropped && !heldByKept) {
                ++left;
                continue;
            }
            out.push_back(std::move(spell));
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // The declutter of the grafted nodes
    // -------------------------------------------------------------------------

    /// One positioned node sent, in the order the reply's positions come back
    struct DeclutterSlot
    {
        std::string school;
        std::size_t index = 0;
    };

    namespace Detail
    {
        inline constexpr double kMaxCoord = 1e6;   // LayoutDeclutter.h: a position past this counts as none

        [[nodiscard]] inline bool Coordinate(const OrderedJson& node, const char* key)
        {
            const auto it = node.find(key);
            return it != node.end() && it->is_number() && std::isfinite(it->get<double>()) &&
                   std::abs(it->get<double>()) <= kMaxCoord;
        }

        /// The nodes before the graft in a school (the grafted ones follow them)
        [[nodiscard]] inline std::size_t Existing(const std::map<std::string, std::size_t>& preCounts,
            const std::string& school, std::size_t fallback)
        {
            const auto it = preCounts.find(school);
            return it != preCounts.end() ? it->second : fallback;
        }
    }

    /// The declutter request (LayoutDeclutter.h) for the whole tree: the nodes a school had
    /// before the graft (preCounts) fixed, the grafted ones free, spread false
    [[nodiscard]] inline Json DeclutterRequest(const OrderedJson& tree, const std::map<std::string, std::size_t>& preCounts,
        std::vector<DeclutterSlot>& slots)
    {
        slots.clear();
        Json schools = Json::array();
        const auto all = tree.is_object() ? tree.find("schools") : tree.end();
        if (tree.is_object() && all != tree.end() && all->is_object()) {
            for (const auto& [name, school] : all->items()) {
                if (!school.is_object() || !school.contains("nodes") || !school["nodes"].is_array()) continue;
                const auto& nodes = school["nodes"];
                const std::size_t existing = Detail::Existing(preCounts, name, nodes.size());
                Json sent = Json::array();
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    const auto& node = nodes[i];
                    if (!node.is_object() || !Detail::Coordinate(node, "x") || !Detail::Coordinate(node, "y")) continue;
                    const bool isRoot = node.contains("isRoot") && node["isRoot"].is_boolean() && node["isRoot"].get<bool>();
                    const bool hasChildren = node.contains("children") && node["children"].is_array();
                    sent.push_back({ { "formId", Detail::Text(node, "formId") }, { "x", node["x"].get<double>() },
                        { "y", node["y"].get<double>() }, { "isRoot", isRoot }, { "fixed", i < existing },
                        { "children", hasChildren ? Json(node["children"]) : Json::array() } });
                    slots.push_back({ name, i });
                }
                Json entry = { { "name", name }, { "nodes", std::move(sent) } };
                for (const char* key : { "startAngle", "endAngle" }) {
                    if (school.contains(key)) entry[key] = Json(school[key]);
                }
                schools.push_back(std::move(entry));
            }
        }
        Json request = { { "id", "tree-graft" }, { "schools", std::move(schools) }, { "spread", false } };
        request["globe"] = tree.is_object() && tree.contains("globe") && tree["globe"].is_object() ? Json(tree["globe"]) : Json::object();
        if (tree.is_object() && tree.contains("layoutMode")) request["layoutMode"] = Json(tree["layoutMode"]);
        request["noRotate"] = tree.is_object() && tree.contains("noRotate") && tree["noRotate"].is_boolean() &&
                              tree["noRotate"].get<bool>();
        return request;
    }

    /// The reply's positions onto the grafted nodes only (the fixed ones come back as they
    /// were sent). An error text, with nothing taken, when the reply does not fit the slots
    [[nodiscard]] inline std::string ApplyDeclutter(OrderedJson& tree, const Json& reply, const std::vector<DeclutterSlot>& slots,
        const std::map<std::string, std::size_t>& preCounts, std::size_t& moved)
    {
        moved = 0;
        const auto positions = reply.is_object() ? reply.find("positions") : reply.end();
        if (!reply.is_object() || positions == reply.end() || !positions->is_array()) return "the reply has no positions";
        if (positions->empty()) return {};   // fewer than two positioned spells: nothing moves
        if (positions->size() != slots.size()) {
            return std::format("{} positions for the {} spells sent", positions->size(), slots.size());
        }
        for (std::size_t k = 0; k < slots.size(); ++k) {
            const auto& p = (*positions)[k];
            if (!p.is_array() || p.size() < 3 || !p[0].is_string() || !p[1].is_number() || !p[2].is_number()) {
                return std::format("position {} cannot be read", k);
            }
            const auto& node = tree["schools"][slots[k].school]["nodes"][slots[k].index];
            if (Detail::Text(node, "formId") != p[0].get<std::string>()) {
                return std::format("position {} is {}, {} was sent there", k, p[0].get<std::string>(), Detail::Text(node, "formId"));
            }
        }
        for (std::size_t k = 0; k < slots.size(); ++k) {
            auto& nodes = tree["schools"][slots[k].school]["nodes"];
            if (slots[k].index < Detail::Existing(preCounts, slots[k].school, nodes.size())) continue;
            auto& node = nodes[slots[k].index];
            const double x = (*positions)[k][1].get<double>(), y = (*positions)[k][2].get<double>();
            if (x != node["x"].get<double>() || y != node["y"].get<double>()) ++moved;
            node["x"] = x;
            node["y"] = y;
        }
        return {};
    }

    // -------------------------------------------------------------------------
    // The plan
    // -------------------------------------------------------------------------

    struct PlanInput
    {
        OrderedJson tree;                  ///< spell_tree.json as read, in its key order
        Json scanSpells = Json::array();   ///< the scan output's "spells" (moved into the graft)
        TreeRecordFile::TreeRecord record;
        Baseline baseline;
        TreeGraft::PlayerRules rules;
        std::vector<NodeVerdict> verdicts; ///< VerdictsOf the validated TaggedCopy of `tree`
        std::string loadOrder;
        std::size_t plugins = 0, spells = 0;
    };

    struct Plan
    {
        OrderedJson tree;                  ///< refreshed, grafted, decluttered
        TreeRecordFile::TreeRecord record;
        bool writeTree = false;            ///< something was grafted
        bool writeRecord = false;          ///< the record changed
        bool complete = false;             ///< nothing deferred or unplaced
        Refresh refresh;
        std::vector<std::string> grafted, leftOut, skippedNoSchool, deferred;
        std::map<std::string, std::size_t> preCounts;   ///< each school's nodes before the graft
        std::size_t candidates = 0, scanLeft = 0, declutterMoved = 0;
        Json declutterSummary = Json::object();         ///< the reply's counts (overlapsLeft: among fixed nodes too)
        std::string problem;               ///< the checks' first failure: nothing may be written
    };

    /// The whole run without the game (see the top). Throws as RefreshIds and Graft do.
    [[nodiscard]] inline Plan PlanGraft(PlanInput input, const TreeBuilder::BuildConfig& config)
    {
        Plan plan;
        const OrderedJson original = input.tree;
        if (original.is_object() && original.contains("schools") && original["schools"].is_object()) {
            for (const auto& [name, school] : original["schools"].items()) {
                if (school.is_object() && school.contains("nodes") && school["nodes"].is_array()) {
                    plan.preCounts[name] = school["nodes"].size();
                }
            }
        }
        plan.refresh = RefreshIds(input.tree, input.verdicts);

        // The candidates are copied out, so the scan's spells can move into the graft's list
        const std::unordered_set<std::string> seen(input.baseline.seen.begin(), input.baseline.seen.end());
        std::vector<Json> candidates;
        for (const auto* spell : TreeGraft::NewSpells(input.scanSpells, input.tree, seen)) candidates.push_back(*spell);
        std::vector<const Json*> pointers;
        pointers.reserve(candidates.size());
        for (const auto& spell : candidates) pointers.push_back(&spell);
        plan.candidates = candidates.size();

        auto result = [&] {
            const auto scan = LiveScanForGraft(std::move(input.scanSpells), input.tree, input.verdicts, plan.scanLeft);
            return TreeGraft::Graft(input.tree, scan, pointers, input.rules, config);
        }();
        plan.tree = std::move(result.tree);
        plan.grafted = std::move(result.grafted);
        plan.leftOut = std::move(result.leftOut);
        plan.skippedNoSchool = std::move(result.skippedNoSchool);
        plan.deferred = std::move(result.deferred);

        if (!plan.grafted.empty()) {
            std::vector<DeclutterSlot> slots;
            const Json reply = LayoutDeclutter::Run(DeclutterRequest(plan.tree, plan.preCounts, slots));
            for (const char* key : { "moved", "rounds", "overlapsLeft", "linesLeft", "ms" }) {
                if (reply.is_object() && reply.contains(key)) plan.declutterSummary[key] = reply[key];
            }
            if (const auto error = ApplyDeclutter(plan.tree, reply, slots, plan.preCounts, plan.declutterMoved); !error.empty()) {
                plan.problem = "the declutter's reply: " + error;
            }
        }
        if (plan.problem.empty()) plan.problem = PreGraftNodesKept(original, plan.tree, RematchedIds(original, input.verdicts));
        if (plan.problem.empty()) plan.problem = SeenGrafted(plan.grafted, input.baseline.seen);

        plan.complete = plan.deferred.empty() && plan.skippedNoSchool.empty();
        plan.record = RecordAfterGraft(input.record, input.baseline, plan.grafted, plan.leftOut, plan.complete,
            input.loadOrder, input.plugins, input.spells);
        plan.writeTree = !plan.grafted.empty();
        plan.writeRecord = TreeRecordFile::ToJson(plan.record) != TreeRecordFile::ToJson(input.record);
        return plan;
    }
}
