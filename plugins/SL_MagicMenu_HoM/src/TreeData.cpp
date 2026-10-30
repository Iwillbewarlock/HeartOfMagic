#include "TreeData.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

// =============================================================================
// TREE DATA - spell_tree.json -> Tree, and the tree window query
// =============================================================================
//
// Ids: a node is keyed by its saved "formId" text (byId); links are resolved by
// that text when it names a node, else by the parsed FormID (Find, preferring the
// link's own school and a non-"_dup_" node). A link that names no node in the tree
// is skipped and counted in Tree::unparsedIds (so is a node with no "formId" text,
// which cannot be linked or found). A node whose id text is not hex is kept with
// formId 0 and left out of byFormId. Every JSON field is type-checked before it
// is read: a wrong-typed field is treated as missing, never thrown on. Schools
// come in nlohmann::json's key order (sorted by name).
//
// Placement: a node's stage offset is its saved x/y minus the centre's, times the
// scale (y down, as saved), not turned (spec v11 T10 / R12).
//
// O(nodes + links) to parse (hash maps); Query is O(nodes) for its per-node marker arrays plus
// O(school nodes + reached edges + k log k) for k candidates (keep is a handful: its rank is a scan);
// StructuralPath is O(nodes + edges log degree).
namespace TreeData
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::string_view kBom = "\xEF\xBB\xBF";
        constexpr std::string_view kDupMarker = "_dup_";
        constexpr std::size_t kMaxHexDigits = 8;

        void SetError(std::string* a_error, std::string a_text)
        {
            if (a_error) {
                *a_error = std::move(a_text);
            }
        }

        /// "0x00012FCD" or "0x00012FCD_dup_2" -> 0x12FCD; 0 if the text is not 1-8 hex digits after an optional 0x
        std::uint32_t ParseFormId(std::string_view a_text)
        {
            if (const auto dup = a_text.find(kDupMarker); dup != std::string_view::npos) {
                a_text = a_text.substr(0, dup);
            }
            if (a_text.size() >= 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X')) {
                a_text.remove_prefix(2);
            }
            if (a_text.empty() || a_text.size() > kMaxHexDigits) {
                return 0;
            }
            std::uint32_t value{};
            const auto* end = a_text.data() + a_text.size();
            const auto [ptr, ec] = std::from_chars(a_text.data(), end, value, 16);
            return (ec == std::errc{} && ptr == end) ? value : 0;
        }

        bool IsDup(std::string_view a_id) { return a_id.find(kDupMarker) != std::string_view::npos; }

        const Json* Field(const Json& a_object, const char* a_key)
        {
            const auto it = a_object.find(a_key);
            return it == a_object.end() ? nullptr : &*it;
        }

        std::string StringField(const Json& a_object, const char* a_key)
        {
            const auto* value = Field(a_object, a_key);
            return (value && value->is_string()) ? value->get<std::string>() : std::string{};
        }

        std::optional<double> NumberField(const Json& a_object, const char* a_key)
        {
            const auto* value = Field(a_object, a_key);
            if (!value || !value->is_number()) {
                return std::nullopt;
            }
            const auto number = value->get<double>();
            return std::isfinite(number) ? std::optional{ number } : std::nullopt;
        }

        int IntField(const Json& a_object, const char* a_key)
        {
            // A number outside int (a hand-edited or corrupt file) is 0: the cast would be undefined
            const auto number = NumberField(a_object, a_key);
            constexpr auto kMin = static_cast<double>(std::numeric_limits<int>::min());
            constexpr auto kMax = static_cast<double>(std::numeric_limits<int>::max());
            return number && *number >= kMin && *number <= kMax ? static_cast<int>(*number) : 0;
        }

        bool BoolField(const Json& a_object, const char* a_key)
        {
            const auto* value = Field(a_object, a_key);
            if (!value) {
                return false;
            }
            if (value->is_boolean()) {
                return value->get<bool>();
            }
            return value->is_number() && value->get<double>() != 0.0;
        }

        /// The string entries of an array field (anything else in it is skipped)
        std::vector<std::string_view> StringList(const Json& a_object, const char* a_key)
        {
            std::vector<std::string_view> out;
            const auto* value = Field(a_object, a_key);
            if (!value || !value->is_array()) {
                return out;
            }
            for (const auto& entry : *value) {
                if (entry.is_string()) {
                    out.emplace_back(entry.get_ref<const std::string&>());
                }
            }
            return out;
        }

        /// A link's node: the exact saved id, else the FormID (same school, non-dup first); -1 if neither
        int Resolve(const Tree& a_tree, std::string_view a_text, std::string_view a_school)
        {
            if (const auto it = a_tree.byId.find(std::string(a_text)); it != a_tree.byId.end()) {
                return it->second;
            }
            const auto formId = ParseFormId(a_text);
            return formId != 0 ? Find(a_tree, formId, a_school) : -1;
        }

        class EdgeBuilder
        {
        public:
            explicit EdgeBuilder(Tree& a_tree) :
                _tree(a_tree) {}

            /// Adds from -> to once per kind; self links are dropped
            void Add(int a_from, int a_to, EdgeKind a_kind)
            {
                if (a_from == a_to) {
                    return;
                }
                const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a_from)) << 32) |
                                 static_cast<std::uint32_t>(a_to);
                auto& seen = a_kind == EdgeKind::kLock ? _lock : _structural;
                if (!seen.insert(key).second) {
                    return;
                }
                _tree.edges.push_back({ a_from, a_to, a_kind });
                ++(a_kind == EdgeKind::kLock ? _tree.lockEdges : _tree.structuralEdges);
            }

        private:
            Tree& _tree;
            std::unordered_set<std::uint64_t> _structural;
            std::unordered_set<std::uint64_t> _lock;
        };

        struct Pending
        {
            int index{};
            const Json* node{};
        };

        void AddNodeEdges(Tree& a_tree, EdgeBuilder& a_edges, const Pending& a_pending)
        {
            const auto& json = *a_pending.node;
            const auto& school = a_tree.nodes[a_pending.index].school;
            const auto link = [&](std::string_view a_text, bool a_textIsParent, EdgeKind a_kind) {
                const auto other = Resolve(a_tree, a_text, school);
                if (other < 0) {
                    ++a_tree.unparsedIds;
                    return;
                }
                a_textIsParent ? a_edges.Add(other, a_pending.index, a_kind) : a_edges.Add(a_pending.index, other, a_kind);
            };

            for (const auto text : StringList(json, "prerequisites")) {
                link(text, true, EdgeKind::kStructural);
            }
            for (const auto text : StringList(json, "children")) {
                link(text, false, EdgeKind::kStructural);
            }
            if (const auto* locks = Field(json, "locks"); locks && locks->is_array()) {
                for (const auto& lock : *locks) {
                    if (lock.is_object()) {
                        if (const auto* id = Field(lock, "nodeId"); id && id->is_string()) {
                            link(id->get_ref<const std::string&>(), true, EdgeKind::kLock);
                        }
                    }
                }
            }
        }

        /// A FormID list without the node's own id text (HoM's WithoutId) and without entries that are not hex
        std::vector<std::uint32_t> OtherIds(const Json& a_object, const char* a_key, std::string_view a_selfId)
        {
            std::vector<std::uint32_t> out;
            for (const auto text : StringList(a_object, a_key)) {
                if (text == a_selfId) {
                    continue;
                }
                if (const auto formId = ParseFormId(text); formId != 0) {
                    out.push_back(formId);
                }
            }
            return out;
        }

        /// The ids of "locks"[].nodeId
        std::vector<std::uint32_t> LockIds(const Json& a_object)
        {
            std::vector<std::uint32_t> out;
            if (const auto* locks = Field(a_object, "locks"); locks && locks->is_array()) {
                for (const auto& lock : *locks) {
                    const auto* id = lock.is_object() ? Field(lock, "nodeId") : nullptr;
                    if (id && id->is_string()) {
                        if (const auto formId = ParseFormId(id->get_ref<const std::string&>()); formId != 0) {
                            out.push_back(formId);
                        }
                    }
                }
            }
            return out;
        }

        std::vector<std::uint32_t> Unique(const std::vector<std::uint32_t>& a_ids)
        {
            std::vector<std::uint32_t> out;
            for (const auto id : a_ids) {
                if (std::ranges::find(out, id) == out.end()) {
                    out.push_back(id);
                }
            }
            return out;
        }

        /// The prerequisites HoM itself evaluates (spelllearning/include/TreePrereqEntries.h, the TreeParser
        /// rules): softNeeded no more than the soft list; a node with neither list but "prerequisites" gets the
        /// first as hard and, with more than one, the rest as soft with 1 needed; a school's root has none; no
        /// repeats, an id in both lists kept in hard (one that is also a lock lowers softNeeded by one, as HoM
        /// does); softNeeded clamped to 0..soft. Ids are compared as FormIDs, HoM compares the saved texts: two
        /// spellings of one FormID ("0x12FCD", "0x00012FCD") count as one here.
        void NormalisePrereqs(Node& a_node, const Json& a_json, std::string_view a_schoolRoot)
        {
            const auto prereqs = OtherIds(a_json, "prerequisites", a_node.id);
            auto hard = OtherIds(a_json, "hardPrereqs", a_node.id);
            auto soft = OtherIds(a_json, "softPrereqs", a_node.id);
            int softNeeded = std::min(IntField(a_json, "softNeeded"), static_cast<int>(soft.size()));

            if (hard.empty() && soft.empty() && !prereqs.empty()) {
                hard.assign(1, prereqs.front());
                if (prereqs.size() > 1) {
                    soft.assign(prereqs.begin() + 1, prereqs.end());
                    softNeeded = 1;
                }
            }
            if (a_node.isRoot || (!a_schoolRoot.empty() && a_node.id == a_schoolRoot)) {
                hard.clear();
                soft.clear();
                softNeeded = 0;
            }

            a_node.hard = Unique(hard);
            const auto locks = LockIds(a_json);
            a_node.soft.clear();
            for (const auto id : Unique(soft)) {
                if (std::ranges::find(a_node.hard, id) == a_node.hard.end()) {
                    a_node.soft.push_back(id);
                } else if (std::ranges::find(locks, id) != locks.end()) {
                    --softNeeded;
                }
            }
            a_node.softNeeded = std::clamp(softNeeded, 0, static_cast<int>(a_node.soft.size()));
        }

        Node MakeNode(const Json& a_json, const std::string& a_school, std::string_view a_schoolRoot)
        {
            Node node;
            node.id = StringField(a_json, "formId");
            node.formId = ParseFormId(node.id);
            node.persistentId = StringField(a_json, "persistentId");
            node.school = a_school;
            node.name = StringField(a_json, "name");
            node.skillLevel = StringField(a_json, "skillLevel");
        node.themeColor = StringField(a_json, "themeColor");
            node.tier = IntField(a_json, "tier");
            const auto x = NumberField(a_json, "x");
            const auto y = NumberField(a_json, "y");
            node.hasXY = x && y;
            if (node.hasXY) {
                node.x = *x;
                node.y = *y;
            }
            node.isRoot = BoolField(a_json, "isRoot");
            NormalisePrereqs(node, a_json, a_schoolRoot);
            return node;
        }

        void ResolveRoots(Tree& a_tree, const std::vector<std::string>& a_rootTexts)
        {
            for (std::size_t s = 0; s < a_tree.schools.size(); ++s) {
                auto& school = a_tree.schools[s];
                if (!a_rootTexts[s].empty()) {
                    const auto index = Resolve(a_tree, a_rootTexts[s], school.name);
                    if (index >= 0 && a_tree.nodes[index].school == school.name) {
                        school.rootIndex = index;
                        continue;
                    }
                }
                const auto it = std::ranges::find_if(school.nodes, [&](int a_index) { return a_tree.nodes[a_index].isRoot; });
                if (it != school.nodes.end()) {
                    school.rootIndex = *it;
                }
            }
        }

        std::optional<Tree> Build(const Json& a_root, std::string* a_error)
        {
            if (!a_root.is_object()) {
                SetError(a_error, "spell tree: the top level is not a JSON object");
                return std::nullopt;
            }
            const auto* schools = Field(a_root, "schools");
            if (!schools || !schools->is_object()) {
                SetError(a_error, "spell tree: no \"schools\" object at the top level");
                return std::nullopt;
            }

            Tree tree;
            tree.noRotate = BoolField(a_root, "noRotate");
            tree.layoutMode = StringField(a_root, "layoutMode");

            std::vector<Pending> pending;
            std::vector<std::string> rootTexts;
            for (const auto& [schoolName, schoolJson] : schools->items()) {
                if (!schoolJson.is_object()) {
                    SetError(a_error, "spell tree: school \"" + schoolName + "\" is not an object");
                    return std::nullopt;
                }
                School school;
                school.name = schoolName;
                school.spokeDeg = NumberField(schoolJson, "spokeAngle");
                rootTexts.push_back(StringField(schoolJson, "root"));

                if (const auto* nodes = Field(schoolJson, "nodes"); nodes && nodes->is_array()) {
                    for (const auto& nodeJson : *nodes) {
                        if (!nodeJson.is_object() || !Field(nodeJson, "formId") || !Field(nodeJson, "formId")->is_string()) {
                            ++tree.unparsedIds;  // a node with no id text cannot be linked or found
                            continue;
                        }
                        const auto index = static_cast<int>(tree.nodes.size());
                        tree.nodes.push_back(MakeNode(nodeJson, schoolName, rootTexts.back()));
                        const auto& node = tree.nodes.back();
                        if (!node.hasXY) {
                            ++tree.nodesWithoutXY;
                        }
                        tree.byId.try_emplace(node.id, index);  // a repeated id text keeps its first node
                        if (node.formId != 0) {
                            tree.byFormId[node.formId].push_back(index);
                        }
                        school.nodes.push_back(index);
                        pending.push_back({ index, &nodeJson });
                    }
                }
                tree.schools.push_back(std::move(school));
            }

            // Links second: a node may name one defined later or in another school
            EdgeBuilder edges(tree);
            for (const auto& entry : pending) {
                AddNodeEdges(tree, edges, entry);
            }
            tree.adjacent.assign(tree.nodes.size(), {});
            for (std::size_t e = 0; e < tree.edges.size(); ++e) {
                tree.adjacent[tree.edges[e].from].push_back(static_cast<int>(e));
                tree.adjacent[tree.edges[e].to].push_back(static_cast<int>(e));
            }
            ResolveRoots(tree, rootTexts);
            return tree;
        }

        struct Offset
        {
            double sx{}, sy{};
        };

        Offset Place(const Node& a_node, const Node& a_center, double a_scale)
        {
            return { (a_node.x - a_center.x) * a_scale, (a_node.y - a_center.y) * a_scale };
        }
    }

    std::optional<Tree> Parse(std::string_view a_json, std::string* a_error)
    {
        if (a_json.starts_with(kBom)) {
            a_json.remove_prefix(kBom.size());
        }
        try {
            const auto root = Json::parse(a_json.begin(), a_json.end(), nullptr, false);
            if (root.is_discarded()) {
                SetError(a_error, "spell tree: not valid JSON");
                return std::nullopt;
            }
            return Build(root, a_error);
        } catch (const Json::exception& e) {
            SetError(a_error, std::string("spell tree: ") + e.what());
            return std::nullopt;
        }
    }

    std::optional<Tree> Load(const std::string& a_path, std::string* a_error)
    {
        // The path is UTF-8 text; a u8string path keeps non-ASCII folders working on Windows
        const std::filesystem::path path(std::u8string(a_path.begin(), a_path.end()));
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            SetError(a_error, "spell tree: cannot open \"" + a_path + "\"");
            return std::nullopt;
        }
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        if (file.bad()) {
            SetError(a_error, "spell tree: read error in \"" + a_path + "\"");
            return std::nullopt;
        }
        return Parse(text, a_error);
    }

    Window Query(const Tree& a_tree, int a_center, View a_view)
    {
        Window window;
        const auto count = static_cast<int>(a_tree.nodes.size());
        if (a_center < 0 || a_center >= count || !a_tree.nodes[a_center].hasXY) {
            return window;  // no centre to measure from
        }
        const auto& center = a_tree.nodes[a_center];
        const auto school = std::ranges::find_if(a_tree.schools, [&](const School& a_school) { return a_school.name == center.school; });

        // Link steps: breadth first over both kinds of edge, either direction, up to maxSteps; nodes without x/y still
        // pass links on. Only marks a node as linked: the window itself is every school node inside it (spec v10 T3)
        std::vector<int> depth(count, -1);
        std::vector<int> reached{ a_center };
        depth[a_center] = 0;
        for (std::size_t head = 0; head < reached.size(); ++head) {
            const auto current = reached[head];
            if (depth[current] >= a_view.maxSteps) {
                continue;
            }
            for (const auto e : a_tree.adjacent[current]) {
                const auto& edge = a_tree.edges[e];
                const auto other = edge.from == current ? edge.to : edge.from;
                if (depth[other] < 0) {
                    depth[other] = depth[current] + 1;
                    reached.push_back(other);
                }
            }
        }

        const auto inside = [&](const Offset& a_offset) {
            return std::abs(a_offset.sx) <= a_view.halfW - a_view.margin && std::abs(a_offset.sy) <= a_view.halfH - a_view.margin;
        };
        std::vector<Offset> offsets(count);
        std::vector<PlacedNode> candidates{ { a_center, 0.0, 0.0, 0 } };
        if (school != a_tree.schools.end()) {
            for (const auto index : school->nodes) {
                const auto& node = a_tree.nodes[index];
                if (index == a_center || !node.hasXY) {
                    continue;
                }
                offsets[index] = Place(node, center, a_view.scale);
                if (inside(offsets[index])) {
                    candidates.push_back({ index, offsets[index].sx, offsets[index].sy, depth[index] });
                }
            }
        }
        window.candidatesBeforeCap = static_cast<int>(candidates.size());

        // The centre stays first; the kept ones next (in a_view.keep's order), the rest nearest first (index breaks ties)
        const auto rank = [&](int a_index) {
            const auto it = std::ranges::find(a_view.keep, a_index);
            return it == a_view.keep.end() ? a_view.keep.size() : static_cast<std::size_t>(it - a_view.keep.begin());
        };
        std::sort(candidates.begin() + 1, candidates.end(), [&](const PlacedNode& a_lhs, const PlacedNode& a_rhs) {
            const auto lhsRank = rank(a_lhs.index);
            const auto rhsRank = rank(a_rhs.index);
            if (lhsRank != rhsRank) {
                return lhsRank < rhsRank;
            }
            const auto lhs = a_lhs.sx * a_lhs.sx + a_lhs.sy * a_lhs.sy;
            const auto rhs = a_rhs.sx * a_rhs.sx + a_rhs.sy * a_rhs.sy;
            return lhs != rhs ? lhs < rhs : a_lhs.index < a_rhs.index;
        });
        // the cap never cuts the centre or a kept one (review v10: iMaxNodes 1 or 2 dropped the selected node or the target);
        // sorted, the kept ones in the window come right after the centre
        std::size_t kept = 1;
        while (kept < candidates.size() && rank(candidates[kept].index) < a_view.keep.size()) ++kept;
        const auto cap = std::max(static_cast<std::size_t>(std::max(1, a_view.maxNodes)), kept);
        if (candidates.size() > cap) {
            candidates.resize(cap);
        }
        window.nodes = std::move(candidates);

        std::vector<char> shown(count, 0);
        for (const auto& placed : window.nodes) {
            shown[placed.index] = 1;
        }
        std::vector<char> taken(a_tree.edges.size(), 0);
        for (const auto& placed : window.nodes) {
            for (const auto e : a_tree.adjacent[placed.index]) {
                const auto& edge = a_tree.edges[e];
                if (taken[e] || !shown[edge.from] || !shown[edge.to]) {
                    continue;
                }
                taken[e] = 1;
                const auto& a = offsets[edge.from];
                const auto& b = offsets[edge.to];
                window.edges.push_back({ e, a.sx, a.sy, b.sx, b.sy });
            }
        }

        // The centre's direct neighbours the window does not draw, by why (spec v10 T7: only "outside" gets a badge)
        std::unordered_set<int> seen;
        for (const auto e : a_tree.adjacent[a_center]) {
            const auto& edge = a_tree.edges[e];
            const auto other = edge.from == a_center ? edge.to : edge.from;
            if (other == a_center || shown[other] || !seen.insert(other).second) {
                continue;
            }
            const auto& node = a_tree.nodes[other];
            if (node.school != center.school) {
                ++window.neighboursOtherSchool;
            } else if (!node.hasXY) {
                ++window.neighboursNoXY;
            } else if (inside(offsets[other])) {
                ++window.neighboursCapped;
            } else {
                window.outside.push_back({ other, offsets[other].sx, offsets[other].sy, depth[other] });
            }
        }
        return window;
    }

    std::vector<int> StructuralPath(const Tree& a_tree, int a_from, int a_to)
    {
        const auto count = static_cast<int>(a_tree.nodes.size());
        if (a_from < 0 || a_from >= count || a_to < 0 || a_to >= count) {
            return {};
        }
        const auto& school = a_tree.nodes[a_from].school;
        if (a_tree.nodes[a_to].school != school) {
            return {};
        }
        // Breadth first; each node's neighbours in index order, so the first parent found is the lowest index
        std::vector<int> parent(count, -1);
        std::vector<int> queue{ a_from };
        parent[a_from] = a_from;
        std::vector<int> next;
        for (std::size_t head = 0; head < queue.size() && parent[a_to] < 0; ++head) {
            const auto current = queue[head];
            next.clear();
            for (const auto e : a_tree.adjacent[current]) {
                const auto& edge = a_tree.edges[e];
                const auto other = edge.from == current ? edge.to : edge.from;
                if (edge.kind == EdgeKind::kStructural && parent[other] < 0 && a_tree.nodes[other].school == school) {
                    next.push_back(other);
                }
            }
            std::ranges::sort(next);
            for (const auto other : next) {
                if (parent[other] < 0) {
                    parent[other] = current;
                    queue.push_back(other);
                }
            }
        }
        if (parent[a_to] < 0) {
            return {};
        }
        std::vector<int> path{ a_to };
        while (path.back() != a_from) {
            path.push_back(parent[path.back()]);
        }
        std::ranges::reverse(path);
        return path;
    }

    std::vector<int> StructuralNeighbours(const Tree& a_tree, int a_center)
    {
        if (a_center < 0 || a_center >= static_cast<int>(a_tree.nodes.size())) {
            return {};
        }
        std::vector<int> out;
        for (const auto e : a_tree.adjacent[a_center]) {
            const auto& edge = a_tree.edges[e];
            if (edge.kind == EdgeKind::kStructural) {
                out.push_back(edge.from == a_center ? edge.to : edge.from);
            }
        }
        std::ranges::sort(out);
        const auto [first, last] = std::ranges::unique(out);
        out.erase(first, last);
        out.insert(out.begin(), a_center);
        return out;
    }

    int Find(const Tree& a_tree, std::uint32_t a_formId, std::string_view a_school)
    {
        const auto it = a_tree.byFormId.find(a_formId);
        if (it == a_tree.byFormId.end()) {
            return -1;
        }
        int best = -1;
        int bestScore = -1;
        for (const auto index : it->second) {
            const auto& node = a_tree.nodes[index];
            const auto score = (!a_school.empty() && node.school == a_school ? 2 : 0) + (IsDup(node.id) ? 0 : 1);
            if (score > bestScore) {
                best = index;
                bestScore = score;
            }
        }
        return best;
    }
}
