#pragma once

#include "TreeRemap.h"
#include "treebuilder/GraftCandidates.h"

#include <cstddef>
#include <format>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// TREE GRAFT CHECKS - what the dev self-test reports after a graft (TreeGraftRun.h,
// step 4); no game types, tools/graft-run-check.cpp runs them offline
// =============================================================================
namespace TreeGraftRun
{
    using Json = nlohmann::json;
    using OrderedJson = nlohmann::ordered_json;

    namespace CheckDetail
    {
        template <class J>
        [[nodiscard]] std::string Text(const J& object, const char* key)
        {
            return TreeGraft::Detail::Text(object, key);
        }

        /// The nodes before the graft in a school (the grafted ones follow them)
        [[nodiscard]] inline std::size_t Existing(const std::map<std::string, std::size_t>& preCounts,
            const std::string& school, std::size_t fallback)
        {
            const auto it = preCounts.find(school);
            return it != preCounts.end() ? it->second : fallback;
        }
    }

    /// Every node `before` had is in `after` at its place with every field as it was, but
    /// for ids (see the top, step 4) and grafted children at the end of a children list.
    /// A persistentId may change only as `rematched` says (the old one A-Z folded -> the
    /// new one: validation found the node again by editor id). Empty when kept, else the
    /// first difference.
    [[nodiscard]] inline std::string PreGraftNodesKept(const OrderedJson& before, const OrderedJson& after,
        const std::map<std::string, std::string>& rematched)
    {
        if (!before.is_object() || !after.is_object()) return "a tree is not an object";
        for (const auto& [key, value] : before.items()) {
            if (key != "schools" && key != "bridges" && (!after.contains(key) || after[key] != value)) {
                return "the tree's " + key + " changed";
            }
        }
        if (!before.contains("schools") || !before["schools"].is_object()) return {};
        const auto& was = before["schools"];
        if (!after.contains("schools") || !after["schools"].is_object() || after["schools"].size() != was.size()) {
            return "the schools changed";
        }
        const auto& now = after["schools"];

        // The id map, from the nodes' own formId changes; and each school's grafted node ids
        std::map<std::string, std::string> ids;
        std::map<std::string, std::set<std::string>> grafted;
        for (const auto& [name, school] : was.items()) {
            if (!now.contains(name)) return "school " + name + " is gone";
            const bool listed = school.is_object() && school.contains("nodes") && school["nodes"].is_array();
            if (!listed) continue;
            const auto& oldNodes = school["nodes"];
            const auto& other = now[name];
            if (!other.is_object() || !other.contains("nodes") || !other["nodes"].is_array() ||
                other["nodes"].size() < oldNodes.size()) {
                return "school " + name + " lost nodes";
            }
            const auto& newNodes = other["nodes"];
            for (std::size_t i = 0; i < oldNodes.size(); ++i) {
                const auto a = CheckDetail::Text(oldNodes[i], "formId"), b = CheckDetail::Text(newNodes[i], "formId");
                if (!oldNodes[i].is_object() || a.empty() || a == b) continue;
                const auto [it, added] = ids.emplace(TreeRemap::NormalizeFormId(a), b);
                if (!added && it->second != b) return std::format("nodes saved as {} now differ ({}, {})", a, it->second, b);
            }
            for (std::size_t i = oldNodes.size(); i < newNodes.size(); ++i) grafted[name].insert(CheckDetail::Text(newNodes[i], "formId"));
        }
        const auto same = [&ids](const OrderedJson& a, const OrderedJson& b) {
            if (a == b) return true;
            if (!a.is_string() || !b.is_string()) return false;
            const auto it = ids.find(TreeRemap::NormalizeFormId(a.get<std::string>()));
            return it != ids.end() && it->second == b.get<std::string>();
        };
        const auto sameList = [&same](const OrderedJson& a, const OrderedJson& b, const std::set<std::string>* extra) {
            if (!a.is_array() || !b.is_array()) return a == b;
            if (b.size() < a.size() || (!extra && b.size() != a.size())) return false;
            for (std::size_t k = 0; k < a.size(); ++k) {
                if (!same(a[k], b[k])) return false;
            }
            for (std::size_t k = a.size(); k < b.size(); ++k) {
                if (!b[k].is_string() || !extra->contains(b[k].get<std::string>())) return false;
            }
            return true;
        };
        const auto sameLocks = [&same](const OrderedJson& a, const OrderedJson& b) {
            if (!a.is_array() || !b.is_array() || a.size() != b.size()) return a == b;
            for (std::size_t k = 0; k < a.size(); ++k) {
                if (!a[k].is_object() || !b[k].is_object() || !a[k].contains("nodeId") || !b[k].contains("nodeId")) {
                    if (a[k] != b[k]) return false;
                    continue;
                }
                OrderedJson x = a[k], y = b[k];
                if (!same(x["nodeId"], y["nodeId"])) return false;
                x.erase("nodeId");
                y.erase("nodeId");
                if (x != y) return false;
            }
            return true;
        };
        const OrderedJson none = OrderedJson::array();

        for (const auto& [name, school] : was.items()) {
            const auto& other = now[name];
            if (!school.is_object()) {
                if (school != other) return "school " + name + " changed";
                continue;
            }
            for (const auto& [key, value] : school.items()) {
                if (key == "nodes") continue;
                if (!other.contains(key) || !(key == "root" ? same(value, other[key]) : value == other[key])) {
                    return "school " + name + "'s " + key + " changed";
                }
            }
            if (other.size() != school.size()) return "school " + name + " gained a field";
            if (!school.contains("nodes") || !school["nodes"].is_array()) continue;
            const auto& oldNodes = school["nodes"];
            const auto& newNodes = other["nodes"];
            for (std::size_t i = 0; i < oldNodes.size(); ++i) {
                const auto& a = oldNodes[i];
                const auto& b = newNodes[i];
                const auto where = std::format("{} node {} ({})", name, i, CheckDetail::Text(a, "formId"));
                if (!a.is_object() || !b.is_object()) {
                    if (a != b) return where + " changed";
                    continue;
                }
                for (const auto& [key, value] : a.items()) {
                    if (!b.contains(key)) return where + " lost " + key;
                    bool ok = false;
                    if (key == "formId") {
                        ok = same(value, b[key]);
                    } else if (key == "persistentId") {
                        const auto it = rematched.find(AsciiText::ToLower(CheckDetail::Text(a, "persistentId")));
                        ok = value == b[key] || (it != rematched.end() && it->second == CheckDetail::Text(b, "persistentId"));
                    } else if (key == "children") {
                        ok = sameList(value, b[key], &grafted[name]);
                    } else if (key == "prerequisites" || key == "softPrereqs" || key == "hardPrereqs") {
                        ok = sameList(value, b[key], nullptr);
                    } else if (key == "locks") {
                        ok = sameLocks(value, b[key]);
                    } else {
                        ok = value == b[key];
                    }
                    if (!ok) return where + ": " + key + " changed";
                }
                for (const auto& [key, value] : b.items()) {
                    if (a.contains(key)) continue;
                    // Graft makes a children list for a parent without one
                    if (key != "children" || !sameList(none, value, &grafted[name])) return where + " gained " + key;
                }
            }
        }

        const auto oldBridges = before.contains("bridges") ? before["bridges"] : OrderedJson();
        const auto newBridges = after.contains("bridges") ? after["bridges"] : OrderedJson();
        if (!oldBridges.is_array() || !newBridges.is_array() || oldBridges.size() != newBridges.size()) {
            return oldBridges == newBridges ? std::string() : std::string("the bridges changed");
        }
        for (std::size_t k = 0; k < oldBridges.size(); ++k) {
            const auto& a = oldBridges[k];
            const auto& b = newBridges[k];
            if (!a.is_object() || !b.is_object() || a.size() != b.size()) {
                if (a != b) return std::format("bridge {} changed", k);
                continue;
            }
            for (const auto& [key, value] : a.items()) {
                const bool end = key == "from" || key == "to";
                if (!b.contains(key) || !(end ? same(value, b[key]) : value == b[key])) return std::format("bridge {} changed", k);
            }
        }
        return {};
    }

    /// Each grafted node (after a school's preCounts) is in the validated tree (the one the
    /// panel gets) under a parent of its school that lists it as a child. Empty, else the first miss.
    [[nodiscard]] inline std::string GraftedHaveLiveParents(const Json& validated, const OrderedJson& grafted,
        const std::map<std::string, std::size_t>& preCounts)
    {
        using TreeGraft::Detail::PersistentKey;
        if (!grafted.is_object() || !grafted.contains("schools") || !grafted["schools"].is_object()) return {};
        for (const auto& [name, school] : grafted["schools"].items()) {
            if (!school.is_object() || !school.contains("nodes") || !school["nodes"].is_array()) continue;
            const auto& nodes = school["nodes"];
            const auto existing = CheckDetail::Existing(preCounts, name, nodes.size());
            if (existing >= nodes.size()) continue;
            const Json* checked = nullptr;
            if (validated.is_object() && validated.contains("schools") && validated["schools"].contains(name)) {
                checked = &validated["schools"][name];
            }
            if (!checked || !checked->is_object() || !checked->contains("nodes") || !(*checked)["nodes"].is_array()) {
                return "school " + name + " is not in the validated tree";
            }
            const auto& live = (*checked)["nodes"];
            const auto find = [&live](auto&& match) -> const Json* {
                for (const auto& node : live) {
                    if (node.is_object() && match(node)) return &node;
                }
                return nullptr;
            };
            for (std::size_t i = existing; i < nodes.size(); ++i) {
                const auto key = PersistentKey(nodes[i]);
                const auto* node = find([&key](const Json& n) { return !key.empty() && PersistentKey(n) == key; });
                const auto label = std::format("grafted {} ({})", CheckDetail::Text(nodes[i], "formId"), name);
                if (!node) return label + " is not in the validated tree";
                const auto prereqs = node->find("prerequisites");
                if (prereqs == node->end() || !prereqs->is_array() || prereqs->empty() || !(*prereqs)[0].is_string()) {
                    return label + " has no parent";
                }
                const auto parentId = TreeRemap::NormalizeFormId((*prereqs)[0].get<std::string>());
                const auto* parent = find([&parentId](const Json& n) {
                    return TreeRemap::NormalizeFormId(CheckDetail::Text(n, "formId")) == parentId;
                });
                if (!parent) return label + "'s parent " + parentId + " is not in its school after validation";
                const auto self = TreeRemap::NormalizeFormId(CheckDetail::Text(*node, "formId"));
                bool listed = false;
                if (parent->contains("children") && (*parent)["children"].is_array()) {
                    for (const auto& child : (*parent)["children"]) {
                        listed = listed || (child.is_string() && TreeRemap::NormalizeFormId(child.get<std::string>()) == self);
                    }
                }
                if (!listed) return label + "'s parent " + parentId + " does not list it";
            }
        }
        return {};
    }

    /// A grafted spell the seen list has (A-Z folded): empty when none
    [[nodiscard]] inline std::string SeenGrafted(const std::vector<std::string>& grafted, const std::vector<std::string>& seen)
    {
        std::unordered_set<std::string> folded;
        for (const auto& id : seen) folded.insert(AsciiText::ToLower(id));
        for (const auto& id : grafted) {
            if (folded.contains(AsciiText::ToLower(id))) return "seen spell " + id + " was grafted";
        }
        return {};
    }
}
