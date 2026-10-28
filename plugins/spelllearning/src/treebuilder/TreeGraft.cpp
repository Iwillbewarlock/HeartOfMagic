#include "treebuilder/TreeGraft.h"
#include "treebuilder/TreeBuilderInternal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>

// =============================================================================
// TREE GRAFT - TreeGraft.h says what goes where; this is how
// =============================================================================

using namespace TreeBuilder;
using namespace TreeBuilder::Internal;

namespace
{
    // One node spacing: a new child starts this far beside its parent (the saved
    // trees' nearest neighbours sit 40-55 apart, parent-child links 50-100)
    constexpr double kChildStep = 55.0;
    // New siblings under one parent fan out across the spoke by this much each
    constexpr double kSiblingSpacing = 45.0;
    // A slot closer than this to a node is passed over for the next one, at most
    // kMaxSlotTries slots (then the last is kept; the declutter parts them)
    constexpr double kMinApart = 20.0;
    constexpr int kMaxSlotTries = 8;
    // The room term (TreeGraft.h): nodes within this distance of a parent's child
    // point count against it, up to kRoomCrowd, kRoomWeight points each
    constexpr double kRoomRadius = 110.0;
    constexpr int kRoomCrowd = 6;
    constexpr float kRoomWeight = 2.0f;
    // A live node without a position: the middle of the room term's range, neutral
    constexpr float kRoomNeutral = kRoomWeight * kRoomCrowd / 2.0f;
    // Positions are written to 2 decimals, as the panel's _round does
    constexpr double kRoundTo = 100.0;
    // A parent this close to the centre has no direction of its own
    constexpr double kMinDirection = 1e-6;

    struct Point
    {
        double x = 0.0;
        double y = 0.0;
    };

    double Distance(const Point& a, const Point& b) { return std::hypot(a.x - b.x, a.y - b.y); }

    template <class Json>
    std::optional<double> Number(const Json& object, const char* key)
    {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_number()) return std::nullopt;
        return it->template get<double>();
    }

    template <class Json>
    std::string Text(const Json& object, const char* key)
    {
        const auto it = object.find(key);
        return (it != object.end() && it->is_string()) ? it->template get<std::string>() : std::string();
    }

    template <class Json>
    std::optional<Point> PositionOf(const Json& node)
    {
        const auto x = Number(node, "x");
        const auto y = Number(node, "y");
        if (!x || !y) return std::nullopt;
        return Point{ *x, *y };
    }

    /// A saved tier (1-5) as a tier index (0-4); nullopt when the node has none
    template <class Json>
    std::optional<int> SavedTierIndex(const Json& node)
    {
        const auto it = node.find("tier");
        if (it == node.end() || !it->is_number_integer()) return std::nullopt;
        return std::clamp(it->template get<int>() - 1, 0, TIER_COUNT - 1);
    }

    /// The settings the tree was built with, where it recorded them (BuildClassic's config block)
    template <class Json>
    BuildConfig TreeConfig(const Json& tree, BuildConfig config)
    {
        const auto saved = tree.find("config");
        if (saved == tree.end() || !saved->is_object()) return config;
        if (const auto mode = Text(*saved, "tier_mode"); !mode.empty()) config.tiersByCost = mode == "cost";
        if (const auto grouping = Number(*saved, "type_grouping")) {
            config.typeGrouping = std::clamp(static_cast<float>(*grouping), 0.0f, BuildConfig::kMaxTypeGrouping);
        }
        return config;
    }

    /// The largest child list of any node: the cap the tree was built (and laid out) under
    template <class Json>
    int LargestChildCount(const Json& schools)
    {
        std::size_t largest = 0;
        for (const auto& [name, school] : schools.items()) {
            if (!school.is_object() || !school.contains("nodes") || !school["nodes"].is_array()) continue;
            for (const auto& node : school["nodes"]) {
                if (node.is_object() && node.contains("children") && node["children"].is_array()) {
                    largest = (std::max)(largest, node["children"].size());
                }
            }
        }
        return static_cast<int>(largest);
    }

    /// The scan by A-Z folded persistentId and by "0x%08X" formId
    struct ScanIndex
    {
        std::unordered_map<std::string, const json*> byPersistent;
        std::unordered_map<std::string, const json*> byFormId;

        explicit ScanIndex(const std::vector<json>& spells)
        {
            for (const auto& spell : spells) {
                if (!spell.is_object()) continue;
                if (const auto key = TreeGraft::Detail::PersistentKey(spell); !key.empty()) byPersistent.emplace(key, &spell);
                if (const auto fid = Text(spell, "formId"); !fid.empty()) {
                    byFormId.emplace(TreeGraft::Detail::SpellIdOf(fid), &spell);
                }
            }
        }

        /// The spell a node stands for when its plugin is loaded (Ruling 1): its persistentId's;
        /// a node without one (a tree from before 2026-10) its formId's
        template <class Json>
        const json* LiveSpell(const Json& node) const
        {
            const auto persistentId = Text(node, "persistentId");
            if (persistentId.find('|') != std::string::npos) {
                const auto it = byPersistent.find(AsciiText::ToLower(persistentId));
                return it != byPersistent.end() ? it->second : nullptr;
            }
            const auto it = byFormId.find(TreeGraft::Detail::SpellIdOf(Text(node, "formId")));
            return it != byFormId.end() ? it->second : nullptr;
        }
    };

    /// The id a result list holds for a spell: its persistentId, else its formId
    std::string ListId(const json& spell)
    {
        const auto persistentId = Text(spell, "persistentId");
        return persistentId.empty() ? Text(spell, "formId") : persistentId;
    }

    /// One school's part of the graft: its live nodes and its new spells
    struct SchoolWork
    {
        std::string name;
        std::vector<std::size_t> liveIndex;   // index into the school's nodes, file order
        std::vector<json> liveSpells;         // each live node's scan spell, its formId the node's id
        std::vector<const json*> news;        // the candidates for this school, scan order
    };

    /// Where a school's nodes sit: the centre they radiate from and each tier's ring
    struct Geometry
    {
        Point centre;
        double fallbackAngle = 0.0;   // radians: the spoke, for a parent on the centre
        std::array<std::optional<std::pair<double, double>>, TIER_COUNT> rings;   // min, max radius
    };

    template <class Json>
    Geometry SchoolGeometry(const Json& tree, const Json& school)
    {
        Geometry geo;
        if (const auto globe = tree.find("globe"); globe != tree.end() && globe->is_object()) {
            geo.centre = { Number(*globe, "x").value_or(0.0), Number(*globe, "y").value_or(0.0) };
        }
        geo.fallbackAngle = Number(school, "rootDirection").value_or(0.0);
        for (const auto& node : school["nodes"]) {
            const auto pos = node.is_object() ? PositionOf(node) : std::nullopt;
            const auto tier = node.is_object() ? SavedTierIndex(node) : std::nullopt;
            if (!pos || !tier) continue;
            const double r = Distance(*pos, geo.centre);
            auto& ring = geo.rings[*tier];
            ring = ring ? std::pair{ (std::min)(ring->first, r), (std::max)(ring->second, r) } : std::pair{ r, r };
        }
        return geo;
    }

    double Round(double v) { return std::round(v * kRoundTo) / kRoundTo; }

    /// The k-th new child's provisional point beside `parent`: one step away from the centre,
    /// fanned across the spoke (0, +1, -1, +2 ...), pulled into its tier's ring when that stays
    /// clear of the parent
    Point ChildPoint(const Point& parent, int k, int tierIdx, const Geometry& geo)
    {
        double dx = parent.x - geo.centre.x;
        double dy = parent.y - geo.centre.y;
        const double length = std::hypot(dx, dy);
        if (length < kMinDirection) {
            dx = std::cos(geo.fallbackAngle);
            dy = std::sin(geo.fallbackAngle);
        } else {
            dx /= length;
            dy /= length;
        }
        const int fan = (k + 1) / 2 * ((k % 2) ? 1 : -1);
        Point q{ parent.x + dx * kChildStep - dy * fan * kSiblingSpacing, parent.y + dy * kChildStep + dx * fan * kSiblingSpacing };

        if (const auto& ring = geo.rings[tierIdx]) {
            const double r = Distance(q, geo.centre);
            const double clamped = std::clamp(r, ring->first, ring->second);
            if (r > kMinDirection && clamped != r) {
                const Point inRing{ geo.centre.x + (q.x - geo.centre.x) / r * clamped,
                    geo.centre.y + (q.y - geo.centre.y) / r * clamped };
                if (Distance(inRing, parent) >= kChildStep / 2) q = inRing;
            }
        }
        return { Round(q.x), Round(q.y) };
    }

    /// The room term per live parent (TreeGraft.h), kept up to date as new nodes land
    class RoomScores
    {
    public:
        explicit RoomScores(const Geometry& geo) : m_geo(geo) {}

        void AddPoint(const Point& p)
        {
            for (auto& [id, spot] : m_spots) {
                if (Distance(spot.childPoint, p) <= kRoomRadius) Set(id, spot, spot.crowd + 1);
            }
            m_points.push_back(p);
        }

        /// A parent's spot: where its next child goes, and how crowded that is (itself not counted)
        void AddParent(const std::string& id, const Point& at, int tierIdx)
        {
            Spot spot{ ChildPoint(at, 0, tierIdx, m_geo), 0 };
            int crowd = 0;
            for (const auto& p : m_points) {
                if (Distance(spot.childPoint, p) <= kRoomRadius && Distance(p, at) > 0.0) ++crowd;
            }
            Set(id, spot, crowd);
            m_spots[id] = spot;
        }

        /// A parent without a position: no spot to measure, the neutral bonus
        void AddUnplaced(const std::string& id) { m_bonus[id] = kRoomNeutral; }

        const std::unordered_map<std::string, float>& Bonus() const { return m_bonus; }

        /// A node of the school sits within `distance` of p
        bool Near(const Point& p, double distance) const
        {
            return std::ranges::any_of(m_points, [&](const Point& q) { return Distance(p, q) < distance; });
        }

    private:
        struct Spot
        {
            Point childPoint;
            int crowd = 0;
        };

        void Set(const std::string& id, Spot& spot, int crowd)
        {
            spot.crowd = crowd;
            m_bonus[id] = kRoomWeight * static_cast<float>((std::max)(0, kRoomCrowd - crowd));
        }

        const Geometry& m_geo;
        std::vector<Point> m_points;
        std::unordered_map<std::string, Spot> m_spots;
        std::unordered_map<std::string, float> m_bonus;
    };

    template <class Json>
    Json NewNodeJson(const TreeNode& node, const json& spell, int tierIdx, const std::string& school,
        const std::string& parentId, const Point& at)
    {
        Json out = Json::object();
        out["formId"] = node.formId;
        out["persistentId"] = Text(spell, "persistentId");
        // As schoolBridges.js applyToOutput bakes them: the editor id finds the node again when its
        // plugin is renamed (empty without powerofthree's Tweaks: not baked), traits feed the viewer's filter
        if (const auto editorId = Text(spell, "editorId"); !editorId.empty()) out["editorId"] = editorId;
        if (const auto traits = spell.find("traits"); traits != spell.end() && !traits->is_null()) out["traits"] = Json(*traits);
        out["name"] = node.name;
        out["tier"] = tierIdx + 1;
        out["school"] = school;
        if (const auto level = Text(spell, "skillLevel"); !level.empty()) out["skillLevel"] = level;
        if (!node.theme.empty()) out["theme"] = node.theme;
        out["children"] = Json::array();
        out["prerequisites"] = Json::array({ parentId });
        out["hardPrereqs"] = Json::array();
        out["softPrereqs"] = Json::array({ parentId });
        out["softNeeded"] = 1;
        out["x"] = at.x;
        out["y"] = at.y;
        return out;
    }

    /// Graft one school's new spells into `school` (the result tree's copy). Returns how many went in.
    /// Takes the work's live spells (moved into the scoring list, then into the nodes).
    template <class Json, class Result>
    std::size_t GraftSchool(const Json& tree, Json& school, SchoolWork& work, const std::vector<std::string>& themes,
        const std::unordered_map<std::string, int>& tiers, const BuildConfig& config, int cap, Result& result)
    {
        auto& nodesJson = school["nodes"];
        const Geometry geo = SchoolGeometry(tree, school);

        // The scoring inputs, rebuilt as BuildClassic builds them per school
        std::vector<json> scoring = std::move(work.liveSpells);
        scoring.reserve(scoring.size() + work.news.size());
        for (const auto* spell : work.news) scoring.push_back(*spell);
        const auto sims = ComputeSimilarityMatrix(scoring);

        std::unordered_map<std::string, TreeNode> nodes;
        std::unordered_map<std::string, std::size_t> jsonIndex;   // node id -> index in nodesJson
        std::vector<std::string> liveIds;
        const std::string rootId = Text(school, "root");
        for (std::size_t i = 0; i < work.liveIndex.size(); ++i) {
            const auto& saved = nodesJson[work.liveIndex[i]];
            TreeNode node = TreeNode::FromSpell(std::move(scoring[i]));   // scored already: the node takes it
            if (nodes.contains(node.formId)) continue;   // a second node with one id: the first one is the parent
            if (saved.contains("children")) {
                for (const auto& child : saved["children"]) {
                    if (child.is_string()) node.children.push_back(child.template get<std::string>());
                }
            }
            // The saved tier; a node without one the tier the build would give it
            const auto computed = tiers.find(node.formId);
            node.depth = SavedTierIndex(saved).value_or(
                computed != tiers.end() ? std::clamp(computed->second, 0, TIER_COUNT - 1) : 0);
            node.isRoot = node.formId == rootId;
            jsonIndex[node.formId] = work.liveIndex[i];
            liveIds.push_back(node.formId);
            nodes.emplace(node.formId, std::move(node));
        }

        std::vector<std::pair<int, const json*>> news;   // tier index, spell; lowest tier first, scan order
        for (const auto* spell : work.news) {
            const auto fid = Text(*spell, "formId");
            if (nodes.contains(fid)) {   // GraftTree defers every saved formId; this only guards the map
                result.deferred.push_back(ListId(*spell));
                continue;
            }
            const auto tier = tiers.find(fid);
            news.emplace_back(tier != tiers.end() ? std::clamp(tier->second, 0, TIER_COUNT - 1) : 0, spell);
            TreeNode node = TreeNode::FromSpell(*spell);
            node.depth = news.back().first;
            nodes.emplace(fid, std::move(node));
        }
        std::ranges::stable_sort(news, {}, &std::pair<int, const json*>::first);

        if (!themes.empty()) {
            for (auto& [fid, node] : nodes) {
                auto [theme, score] = GetSpellPrimaryTheme(node.spellData, themes);
                node.theme = (score > 30) ? theme : "";
                node.themes = GetSpellThemes(node.spellData, themes);
            }
        }
        DropCommonThemes(nodes, config.commonThemeShare);
        const TypeGrouping grouping(nodes, config.typeGrouping);

        RoomScores room(geo);
        for (const auto& node : nodesJson) {
            if (const auto pos = node.is_object() ? PositionOf(node) : std::nullopt) room.AddPoint(*pos);
        }
        std::unordered_map<int, std::vector<TreeNode*>> available;
        std::unordered_map<std::string, Point> positions;
        for (const auto& id : liveIds) {
            auto& node = nodes.at(id);
            const auto pos = PositionOf(nodesJson[jsonIndex.at(id)]);
            if (pos) {
                positions[id] = *pos;
                room.AddParent(id, *pos, (std::min)(node.depth + 1, TIER_COUNT - 1));
            } else {
                room.AddUnplaced(id);
            }
            // Full ones too: FindBestClassicParent's last resort takes a parent past the cap
            // (never one above the spell's tier) before it leaves a spell out
            available[node.depth].push_back(&node);
        }

        std::unordered_map<std::string, int> newChildren;   // parent id -> new children placed under it
        std::size_t placed = 0;
        for (const auto& [tierIdx, spell] : news) {
            auto& node = nodes.at(Text(*spell, "formId"));
            TreeNode* parent =
                FindBestClassicParent(node, available, tierIdx, cap, sims, grouping, nullptr, &room.Bonus());
            if (!parent) {
                result.skippedNoSchool.push_back(ListId(*spell));
                continue;
            }

            parent->AddChild(node.formId);
            node.AddPrerequisite(parent->formId);
            available[tierIdx].push_back(&node);

            // The next fan slot that is not on top of a node (the ring can pull two slots together)
            const auto parentPos = positions.find(parent->formId);
            const Point base = parentPos != positions.end() ? parentPos->second : geo.centre;
            int& slot = newChildren[parent->formId];
            Point at = ChildPoint(base, slot++, tierIdx, geo);
            for (int tries = 1; tries < kMaxSlotTries && room.Near(at, kMinApart); ++tries) {
                at = ChildPoint(base, slot++, tierIdx, geo);
            }
            auto& parentJson = nodesJson[jsonIndex.at(parent->formId)];
            if (!parentJson.contains("children")) parentJson["children"] = Json::array();
            parentJson["children"].push_back(node.formId);

            jsonIndex[node.formId] = nodesJson.size();
            nodesJson.push_back(NewNodeJson<Json>(node, *spell, tierIdx, work.name, parent->formId, at));
            positions[node.formId] = at;
            room.AddPoint(at);
            room.AddParent(node.formId, at, (std::min)(tierIdx + 1, TIER_COUNT - 1));
            result.grafted.push_back(ListId(*spell));
            ++placed;
        }
        return placed;
    }

    template <class Json>
    TreeGraft::BasicGraftResult<Json> GraftTree(const Json& tree, const std::vector<json>& scanSpells,
        const std::vector<const json*>& candidates, const TreeGraft::PlayerRules& rules, const BuildConfig& given)
    {
        const auto startTime = std::chrono::steady_clock::now();
        TreeGraft::BasicGraftResult<Json> result;
        result.tree = tree;

        const bool hasSchools = tree.is_object() && tree.contains("schools") && tree["schools"].is_object();
        const ScanIndex scan(scanSpells);

        // Every formId the file holds, live or not, any school: a candidate with one is deferred
        // (TreeGraft.h); the first node holding it (its persistentId, else its school), for the log
        std::unordered_map<std::string, std::string> savedFormIds;
        if (hasSchools) {
            for (const auto& [name, school] : tree["schools"].items()) {
                if (!school.is_object() || !school.contains("nodes") || !school["nodes"].is_array()) continue;
                for (const auto& node : school["nodes"]) {
                    if (const auto fid = node.is_object() ? Text(node, "formId") : std::string(); !fid.empty()) {
                        const auto holder = Text(node, "persistentId");
                        savedFormIds.emplace(TreeGraft::Detail::SpellIdOf(fid),
                            holder.empty() ? "a node of " + name + " without a persistentId" : holder);
                    }
                }
            }
        }

        // Each school the candidates name, with its live nodes (file order)
        std::vector<SchoolWork> work;
        std::unordered_map<std::string, std::size_t> workOf;
        std::unordered_set<std::string> judged;
        for (const auto* spell : candidates) {
            if (!spell || !spell->is_object() || !judged.insert(ListId(*spell)).second) continue;
            if (TreeGraft::Detail::PersistentKey(*spell).empty()) {
                logger::info("TreeGraft: {} deferred - it has no persistentId", ListId(*spell));
                result.deferred.push_back(ListId(*spell));
                continue;
            }
            if (const auto holder = savedFormIds.find(TreeGraft::Detail::SpellIdOf(Text(*spell, "formId")));
                holder != savedFormIds.end()) {
                // Decision 4 keeps a missing plugin's node in the file: the wait lasts while that node keeps the id
                logger::info("TreeGraft: {} deferred - its formId {} is held by the tree's node {}; it waits while that "
                             "node keeps the id", ListId(*spell), Text(*spell, "formId"), holder->second);
                result.deferred.push_back(ListId(*spell));
                continue;
            }
            if (!TreeGraft::IsTreeCandidate(*spell, rules)) {
                result.leftOut.push_back(ListId(*spell));
                continue;
            }
            const auto schoolName = Text(*spell, "school");
            auto found = workOf.find(schoolName);
            if (found == workOf.end() && hasSchools && tree["schools"].contains(schoolName)) {
                SchoolWork school{ schoolName, {}, {}, {} };
                const auto& schoolJson = tree["schools"][schoolName];
                const auto nodes = schoolJson.is_object() ? schoolJson.find("nodes") : schoolJson.end();
                if (nodes != schoolJson.end() && nodes->is_array()) {
                    for (std::size_t i = 0; i < nodes->size(); ++i) {
                        const auto& node = (*nodes)[i];
                        const auto fid = node.is_object() ? Text(node, "formId") : std::string();
                        const json* live = fid.empty() ? nullptr : scan.LiveSpell(node);
                        // A malformed children field is not touched: such a node is no parent
                        if (!live || (node.contains("children") && !node["children"].is_array())) continue;
                        json copy = *live;
                        copy["formId"] = fid;   // scored and linked under the id the file has
                        school.liveIndex.push_back(i);
                        school.liveSpells.push_back(std::move(copy));
                    }
                }
                found = workOf.emplace(schoolName, work.size()).first;
                work.push_back(std::move(school));
            }
            if (found == workOf.end() || work[found->second].liveIndex.empty()) {
                result.skippedNoSchool.push_back(ListId(*spell));
                continue;
            }
            work[found->second].news.push_back(spell);
        }

        const BuildConfig config = TreeConfig(tree, given);
        const int cap = (std::max)(config.maxChildrenPerNode, hasSchools ? LargestChildCount(tree["schools"]) : 0);

        // Themes and tiers over every spell the touched schools score (live nodes and new
        // spells), as BuildClassic finds them over the spells it builds from (pointers: no copies)
        std::vector<const json*> all;
        for (const auto& school : work) {
            if (school.news.empty()) continue;
            for (const auto& spell : school.liveSpells) all.push_back(&spell);
            all.insert(all.end(), school.news.begin(), school.news.end());
        }
        const auto themes = MergeWithHints(DiscoverThemesPerSchool(all, config.topThemesPerSchool),
            config.topThemesPerSchool + 4);
        const auto tiers = SpellTiers(all, config.tiersByCost);
        all.clear();   // GraftSchool moves the live spells it points to

        for (auto& school : work) {
            if (school.news.empty()) continue;
            const auto themeIt = themes.find(school.name);
            const std::vector<std::string> schoolThemes = themeIt != themes.end() ? themeIt->second : std::vector<std::string>{};
            const auto skippedBefore = result.skippedNoSchool.size();
            const auto placed = GraftSchool(tree, result.tree["schools"][school.name], school, schoolThemes, tiers, config,
                cap, result);
            logger::info("TreeGraft: {} - {} new spells grafted, {} with no parent that could take them", school.name,
                placed, result.skippedNoSchool.size() - skippedBefore);
        }

        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
        logger::info("TreeGraft: {} grafted, {} left out by the player's rules, {} with no school or parent in the tree, "
                     "{} deferred (no persistentId, or a formId the tree file holds) (child cap {}, {:.0f} ms)",
            result.grafted.size(), result.leftOut.size(), result.skippedNoSchool.size(), result.deferred.size(), cap, ms);
        return result;
    }
}

namespace TreeGraft
{
    GraftResult Graft(const nlohmann::json& tree, const std::vector<nlohmann::json>& scanSpells,
        const std::vector<const nlohmann::json*>& candidates, const PlayerRules& rules, const BuildConfig& config)
    {
        return GraftTree(tree, scanSpells, candidates, rules, config);
    }

    OrderedGraftResult Graft(const nlohmann::ordered_json& tree, const std::vector<nlohmann::json>& scanSpells,
        const std::vector<const nlohmann::json*>& candidates, const PlayerRules& rules, const BuildConfig& config)
    {
        return GraftTree(tree, scanSpells, candidates, rules, config);
    }
}
