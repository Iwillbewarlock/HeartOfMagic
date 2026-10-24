#pragma once

#include "FormIdText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// =============================================================================
// TREE PREREQ ENTRIES - the panel's SetTreePrerequisites payload, made without the panel
// =============================================================================
//
// The panel turns the saved tree it is sent (UIManager::OnLoadSpellTree) into the
// array it hands to SetTreePrerequisites after {clear: true}. This makes the same
// array from the same JSON text, so the plugin can fill the tree state at game
// start (UIManagerTreeState.cpp) and tools/tree-prereq-check can compare it with
// the panel's own code. The JS it follows, rule by rule:
//   cppCallbacks.js updateTreeData   no schools (or none listed): nothing sent, not even the clear
//   treeLoad.js loadTreeData         the fast path: trustPrereqs, or the first school's first node has "x"
//   treeLoad.js _loadTrustedTree     per node: TreeParser.withoutId on the three lists, clampSoftNeeded,
//                                    the fallback from "prerequisites", roots emptied, PrereqEdit.repair
//   treeLoad.js sendTreePrerequisites  "_dup_" copies, the canonical lists
// Ids are compared as the JS compares them: exact strings, no hex normalising
// ("0x0001ABCD" and "0001abcd" are two ids here, as they are to the panel). Schools
// go in the order JS walks the parsed object (array-index keys first, ascending, then
// the rest as written), so the caller passes an ordered_json parsed from that text.
//
// What it does not model is refused, never guessed (Outcome::NotModelled): a truthy
// formId that is not a string, a softNeeded that is neither a number nor falsy, an
// object standing in for a list with a "length" key, string locks with non-ASCII
// letters. A tree the panel's own code would throw on sends nothing
// (Outcome::PanelThrows): a null school or node, a non-empty string or a
// {"length": n} object as a list, a school named "hasOwnProperty" (the loaders
// call data.schools.hasOwnProperty), and a value its log lines or isNaN must
// turn into a string or number when JS cannot (an object with its own
// "toString", or an array holding one): trustPrereqs; a school's root, color,
// spokeAngle, startAngle, endAngle; the x, y, name, tier of a school's first
// kLoggedNodesPerSchool nodes. Code of the panel's that has nothing to do with
// the prerequisites (renderers, BridgeView) is assumed not to throw.
//
// O(nodes * list length^2) for the per-node list work (the JS uses indexOf), O(nodes) otherwise.
namespace TreePrereqEntries
{
    using OJson = nlohmann::ordered_json;

    enum class Outcome
    {
        Built,        ///< `entries` is what the panel sends after {clear: true}
        NoSchools,    ///< updateTreeData stops before loading: the panel sends nothing at all
        SlowPath,     ///< loadTreeData goes through TreeParser (not modelled)
        PanelThrows,  ///< the panel's code throws on this tree: it sends nothing
        NotModelled,  ///< a value this does not model (see above)
    };

    struct Result
    {
        Outcome outcome = Outcome::NotModelled;
        OJson entries = OJson::array();  ///< Built: {formId, hardPrereqs, softPrereqs, softNeeded} per entry
        std::string reason;              ///< not Built: why, for the log
    };

    namespace Detail
    {
        /// Object.prototype's names: `{}[name]` is truthy for them, so the panel's plain-object
        /// sets (plain, sentIds, seen) hold them from the start
        inline constexpr std::array<std::string_view, 12> kObjectPrototypeNames = {
            "constructor", "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "toString", "toLocaleString",
            "valueOf", "__proto__", "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__",
        };

        /// A JS object used as a set of strings (`if (set[key])`)
        class JsKeySet
        {
        public:
            [[nodiscard]] bool Has(const std::string& key) const
            {
                return m_keys.contains(key) ||
                       std::find(kObjectPrototypeNames.begin(), kObjectPrototypeNames.end(), key) != kObjectPrototypeNames.end();
            }
            void Add(const std::string& key) { m_keys.insert(key); }

        private:
            std::unordered_set<std::string> m_keys;
        };

        struct Failure
        {
            Outcome outcome;
            std::string reason;
        };

        /// JS truthiness of a parsed JSON value
        [[nodiscard]] inline bool Truthy(const OJson& value)
        {
            switch (value.type()) {
                case OJson::value_t::null: return false;
                case OJson::value_t::boolean: return value.get<bool>();
                case OJson::value_t::number_integer:
                case OJson::value_t::number_unsigned:
                case OJson::value_t::number_float: return value.get<double>() != 0.0;
                case OJson::value_t::string: return !value.get_ref<const std::string&>().empty();
                default: return true;  // arrays and objects, even empty
            }
        }

        [[nodiscard]] inline const OJson* Member(const OJson& object, const char* key)
        {
            if (!object.is_object()) return nullptr;
            const auto it = object.find(key);
            return it == object.end() ? nullptr : &*it;
        }

        /// A canonical array index ("0", "17"; below 2^32 - 1): JS lists such keys first, ascending
        [[nodiscard]] inline std::optional<std::uint32_t> ArrayIndexKey(const std::string& key)
        {
            constexpr std::uint32_t kMaxArrayIndex = (std::numeric_limits<std::uint32_t>::max)() - 1;
            if (key.empty() || (key.size() > 1 && key[0] == '0')) return std::nullopt;
            std::uint64_t value = 0;
            const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
            if (error != std::errc() || end != key.data() + key.size() || value > kMaxArrayIndex) return std::nullopt;
            return static_cast<std::uint32_t>(value);
        }

        /// The values `for (var k in object)` visits, in its order (objects only)
        [[nodiscard]] inline std::vector<const OJson*> ForInValues(const OJson& object)
        {
            std::vector<std::pair<std::uint32_t, const OJson*>> indexed;
            std::vector<const OJson*> named;
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (const auto index = ArrayIndexKey(it.key())) {
                    indexed.emplace_back(*index, &it.value());
                } else {
                    named.push_back(&it.value());
                }
            }
            std::stable_sort(indexed.begin(), indexed.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<const OJson*> values;
            values.reserve(indexed.size() + named.size());
            for (const auto& [index, value] : indexed) values.push_back(value);
            values.insert(values.end(), named.begin(), named.end());
            return values;
        }

        /// _loadTrustedTree logs the first nodes of each school with their x, y, name and tier
        inline constexpr std::size_t kLoggedNodesPerSchool = 3;

        /// A value JS cannot turn into a string or number (ToPrimitive throws): an object with its
        /// own "toString" (never a function in JSON; Object.prototype.valueOf returns the object),
        /// or an array holding one at any depth (join converts each element)
        [[nodiscard]] inline bool HasNoPrimitive(const OJson& value)
        {
            if (value.is_object()) return value.contains("toString");
            if (!value.is_array()) return false;
            return std::any_of(value.begin(), value.end(), [](const OJson& entry) { return HasNoPrimitive(entry); });
        }

        /// The panel's code throws when any of these members of `object` goes into a log line,
        /// isNaN or a property key and cannot be converted
        [[nodiscard]] inline std::optional<Failure> Unconvertible(const OJson& object,
                                                                  std::initializer_list<const char*> keys, const char* what)
        {
            for (const char* key : keys) {
                const auto* value = Member(object, key);
                if (value && HasNoPrimitive(*value)) {
                    return Failure{ Outcome::PanelThrows, std::string(what) + " " + key + " cannot be made a string" };
                }
            }
            return std::nullopt;
        }

        /// A value the panel reads `.length` and indexes: an object with a "length" key is not modelled
        [[nodiscard]] inline std::optional<Failure> LengthObject(const OJson* value, const char* what)
        {
            if (value && value->is_object() && value->contains("length")) {
                return Failure{ Outcome::NotModelled, std::string(what) + " is an object with a \"length\" key" };
            }
            return std::nullopt;
        }

        /// TreeParser.withoutId(list, id): the string entries other than `id`. A non-empty string,
        /// or an object whose "length" is truthy, makes the JS call `.filter` that is not there
        [[nodiscard]] inline std::optional<Failure> WithoutId(const OJson* list, const std::string* id,
                                                              const char* what, std::vector<std::string>& out)
        {
            out.clear();
            if (!list || !Truthy(*list)) return std::nullopt;
            if (list->is_string()) return Failure{ Outcome::PanelThrows, std::string(what) + " is a string" };
            if (list->is_object()) {
                const auto* length = Member(*list, "length");
                if (length && Truthy(*length)) return Failure{ Outcome::PanelThrows, std::string(what) + " is an object with a length" };
                return std::nullopt;
            }
            if (!list->is_array()) return std::nullopt;
            for (const auto& entry : *list) {
                if (!entry.is_string()) continue;
                const auto& text = entry.get_ref<const std::string&>();
                if (id && text == *id) continue;
                out.push_back(text);
            }
            return std::nullopt;
        }

        /// PrereqEdit._lockIds, the string ids only: the JS compares with indexOf, so no other value can match
        [[nodiscard]] inline std::optional<Failure> LockIds(const OJson* locks, std::vector<std::string>& out)
        {
            out.clear();
            if (!locks || !Truthy(*locks)) return std::nullopt;
            if (auto failure = LengthObject(locks, "locks")) return failure;
            if (locks->is_string()) {
                // A string's letters, one id each (UTF-16 units in JS: ASCII only here)
                constexpr unsigned char kFirstNonAsciiByte = 0x80;
                for (const char letter : locks->get_ref<const std::string&>()) {
                    if (static_cast<unsigned char>(letter) >= kFirstNonAsciiByte) return Failure{ Outcome::NotModelled, "locks is a non-ASCII string" };
                    out.emplace_back(1, letter);
                }
                return std::nullopt;
            }
            if (!locks->is_array()) return std::nullopt;
            for (const auto& lock : *locks) {
                const OJson* id = lock.is_object() ? Member(lock, "nodeId") : &lock;
                if (id && id->is_string()) out.push_back(id->get<std::string>());
            }
            return std::nullopt;
        }

        [[nodiscard]] inline bool Contains(const std::vector<std::string>& list, const std::string& id)
        {
            return std::find(list.begin(), list.end(), id) != list.end();
        }

        /// PrereqEdit.unique
        [[nodiscard]] inline std::vector<std::string> Unique(const std::vector<std::string>& list)
        {
            std::vector<std::string> out;
            for (const auto& id : list) {
                if (!Contains(out, id)) out.push_back(id);
            }
            return out;
        }

        /// _canonicalPrereqId: the text before an edit mode copy's first "_dup_"
        [[nodiscard]] inline std::string Canonical(const std::string& id)
        {
            return id.substr(0, id.find(FormIdText::kCopySuffix));
        }

        /// _canonicalPrereqList: copies' ids made their spell's, each once
        [[nodiscard]] inline OJson CanonicalList(const std::vector<std::string>& list)
        {
            OJson out = OJson::array();
            JsKeySet seen;
            for (const auto& id : list) {
                auto canon = Canonical(id);
                if (seen.Has(canon)) continue;
                seen.Add(canon);
                out.push_back(std::move(canon));
            }
            return out;
        }

        /// A JS number as JSON.stringify writes it: an integer when it is one
        [[nodiscard]] inline OJson NumberJson(double value)
        {
            constexpr double kInt64Limit = 9.2e18;
            if (std::trunc(value) == value && std::fabs(value) < kInt64Limit) return static_cast<std::int64_t>(value);
            return value;
        }

        /// One node after _loadTrustedTree: its own id and the lists it sends
        struct Node
        {
            std::string id;
            std::vector<std::string> hard;
            std::vector<std::string> soft;
            double softNeeded = 0.0;
        };

        /// One node of _loadTrustedTree. `node` stays empty for a node sendTreePrerequisites skips (no formId)
        [[nodiscard]] inline std::optional<Failure> ReadNode(const OJson& nd, const OJson* schoolRoot, std::optional<Node>& node)
        {
            node.reset();
            if (nd.is_null()) return Failure{ Outcome::PanelThrows, "a node is null" };
            if (!nd.is_object()) return std::nullopt;  // nd.formId is undefined: never sent

            const auto* formId = Member(nd, "formId");
            const bool hasId = formId && Truthy(*formId);
            if (hasId && !formId->is_string()) return Failure{ Outcome::NotModelled, "a formId is not a string" };
            const std::string* id = hasId ? &formId->get_ref<const std::string&>() : nullptr;

            std::vector<std::string> prereqs, hard, soft, children, lockIds;
            if (auto f = WithoutId(Member(nd, "prerequisites"), id, "prerequisites", prereqs)) return f;
            if (auto f = WithoutId(Member(nd, "hardPrereqs"), id, "hardPrereqs", hard)) return f;
            if (auto f = WithoutId(Member(nd, "softPrereqs"), id, "softPrereqs", soft)) return f;
            if (auto f = WithoutId(Member(nd, "children"), id, "children", children)) return f;
            if (auto f = LockIds(Member(nd, "locks"), lockIds)) return f;
            if (!id) return std::nullopt;

            // TreeParser.clampSoftNeeded: falsy is 0, then no more than the soft list
            double softNeeded = 0.0;
            if (const auto* raw = Member(nd, "softNeeded"); raw && Truthy(*raw)) {
                if (!raw->is_number()) return Failure{ Outcome::NotModelled, "a softNeeded is not a number" };
                softNeeded = raw->get<double>();
            }
            if (softNeeded > static_cast<double>(soft.size())) softNeeded = static_cast<double>(soft.size());

            // Hard and soft empty: one prerequisite is hard; more: the first hard, the rest soft, 1 needed
            if (hard.empty() && soft.empty()) {
                if (prereqs.size() == 1) {
                    hard = prereqs;
                } else if (prereqs.size() > 1) {
                    hard.assign(1, prereqs.front());
                    soft.assign(prereqs.begin() + 1, prereqs.end());
                    softNeeded = 1.0;
                }
            }

            // A school's entry point never has prerequisites (nd.isRoot || id === sd.root)
            const auto* isRoot = Member(nd, "isRoot");
            const bool rootById = schoolRoot && schoolRoot->is_string() && schoolRoot->get_ref<const std::string&>() == *id;
            if ((isRoot && Truthy(*isRoot)) || rootById) {
                hard.clear();
                soft.clear();
                softNeeded = 0.0;
            }

            // PrereqEdit.repair: no repeats, an id in both lists kept in hard; each such id that is
            // also a lock already counted toward softNeeded, which goes down by one for it
            auto uniqueHard = Unique(hard);
            const auto allSoft = Unique(soft);
            std::vector<std::string> keptSoft;
            int lockMoved = 0;
            for (const auto& softId : allSoft) {
                if (!Contains(uniqueHard, softId)) {
                    keptSoft.push_back(softId);
                } else if (Contains(lockIds, softId)) {
                    ++lockMoved;
                }
            }
            if (lockMoved > 0) softNeeded -= lockMoved;
            const auto maxNeeded = static_cast<double>(keptSoft.size());
            softNeeded = softNeeded > maxNeeded ? maxNeeded : (softNeeded < 0.0 ? 0.0 : softNeeded);

            node = Node{ *id, std::move(uniqueHard), std::move(keptSoft), softNeeded };
            return std::nullopt;
        }

        /// The nodes `sd.nodes || []` lists, or nothing for a value that holds none
        [[nodiscard]] inline std::optional<Failure> SchoolNodes(const OJson& school, const OJson*& nodes)
        {
            nodes = nullptr;
            if (school.is_null()) return Failure{ Outcome::PanelThrows, "a school is null" };
            const auto* list = Member(school, "nodes");
            if (auto failure = LengthObject(list, "a school's nodes")) return failure;
            if (list && list->is_array()) nodes = list;  // a string's letters are nodes without a formId
            return std::nullopt;
        }

        /// loadTreeData's fast path test (trustPrereqs, or the first school's first node has "x")
        [[nodiscard]] inline std::optional<Failure> UsesFastPath(const OJson& tree, const std::vector<const OJson*>& schools, bool& fast)
        {
            const auto* trust = Member(tree, "trustPrereqs");
            fast = trust && Truthy(*trust);
            if (fast || schools.empty()) return std::nullopt;
            const OJson* nodes = nullptr;
            if (auto failure = SchoolNodes(*schools.front(), nodes)) return failure;
            if (!nodes || nodes->empty()) return std::nullopt;
            const auto& first = nodes->front();
            if (first.is_null()) return Failure{ Outcome::PanelThrows, "the first school's first node is null" };
            fast = first.is_object() && first.contains("x");
            return std::nullopt;
        }

        /// The schools `for (var name in data.schools)` visits; a string's letters hold no nodes
        [[nodiscard]] inline std::vector<const OJson*> SchoolValues(const OJson& schools)
        {
            if (schools.is_object()) return ForInValues(schools);
            std::vector<const OJson*> values;
            if (schools.is_array()) {
                for (const auto& school : schools) values.push_back(&school);
            }
            return values;
        }

        /// updateTreeData's test: Object.keys(data.schools).length > 0
        [[nodiscard]] inline bool ListsSchools(const OJson* schools)
        {
            if (!schools || !Truthy(*schools)) return false;
            // Keys: an object's names, an array's indices, a string's letters; none for a number or true
            return ((schools->is_object() || schools->is_array()) && !schools->empty()) || schools->is_string();
        }
    }

    /// The array the panel's fast path sends to SetTreePrerequisites for this tree, parsed in
    /// the panel's key order from the text it is sent
    [[nodiscard]] inline Result BuildPrerequisiteEntries(const OJson& tree)
    {
        using namespace Detail;
        Result result;
        const auto* schoolsJson = Member(tree, "schools");
        if (!ListsSchools(schoolsJson)) {
            result.outcome = Outcome::NoSchools;
            result.reason = "the tree lists no schools";
            return result;
        }
        const auto schools = SchoolValues(*schoolsJson);
        auto fail = [&result](Failure failure) {
            result.outcome = failure.outcome;
            result.reason = std::move(failure.reason);
            return result;
        };

        // loadTreeData logs trustPrereqs before anything else; then both loaders call
        // data.schools.hasOwnProperty on their first school, which a school of that name hides
        if (auto failure = Unconvertible(tree, { "trustPrereqs" }, "the tree's")) return fail(std::move(*failure));
        if (schoolsJson->is_object() && schoolsJson->contains("hasOwnProperty")) {
            return fail({ Outcome::PanelThrows, "a school is named hasOwnProperty (data.schools.hasOwnProperty is not a function)" });
        }

        bool fast = false;
        if (auto failure = UsesFastPath(tree, schools, fast)) return fail(std::move(*failure));
        if (!fast) return fail({ Outcome::SlowPath, "no trustPrereqs and the first school's first node has no x" });

        std::vector<Node> nodes;
        for (const auto* school : schools) {
            const OJson* list = nullptr;
            std::optional<Failure> failure = SchoolNodes(*school, list);
            if (!failure) failure = Unconvertible(*school, { "root", "color", "spokeAngle", "startAngle", "endAngle" }, "a school's");
            for (std::size_t i = 0; !failure && list && i < list->size(); ++i) {
                const auto& nd = (*list)[i];
                if (i < kLoggedNodesPerSchool) failure = Unconvertible(nd, { "x", "y", "name", "tier" }, "a node's");
                if (failure) break;
                std::optional<Node> node;
                failure = ReadNode(nd, Member(*school, "root"), node);
                if (node) nodes.push_back(std::move(*node));
            }
            if (failure) return fail(std::move(*failure));
        }

        // sendTreePrerequisites: a copy ("<id>_dup_<n>") only for a spell with no node of its own,
        // and only the first copy; a spell's own node goes every time (C++ keeps the last)
        JsKeySet plain;
        for (const auto& node : nodes) {
            if (Canonical(node.id) == node.id) plain.Add(node.id);
        }
        JsKeySet sent;
        for (const auto& node : nodes) {
            const auto canon = Canonical(node.id);
            if (canon != node.id) {
                if (plain.Has(canon) || sent.Has(canon)) continue;
                sent.Add(canon);
            }
            OJson entry = OJson::object();
            entry["formId"] = canon;
            entry["hardPrereqs"] = CanonicalList(node.hard);
            entry["softPrereqs"] = CanonicalList(node.soft);
            entry["softNeeded"] = NumberJson(node.softNeeded);
            result.entries.push_back(std::move(entry));
        }
        result.outcome = Outcome::Built;
        return result;
    }

    [[nodiscard]] inline const char* OutcomeName(Outcome outcome) noexcept
    {
        switch (outcome) {
            case Outcome::Built: return "built";
            case Outcome::NoSchools: return "no schools";
            case Outcome::SlowPath: return "slow path";
            case Outcome::PanelThrows: return "the panel would throw";
            case Outcome::NotModelled: return "not modelled";
        }
        return "?";
    }
}
