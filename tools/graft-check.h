#pragma once

// graft-check's shared part (graft-check.cpp runs the cases; graft-check-cases.cpp holds the
// tree graft's placement cases): the checker, made-up scans and trees, and what the cases
// compare them with.

#include "Common.h"

#include "treebuilder/GraftCandidates.h"
#include "treebuilder/TreeGraft.h"

#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace GraftCheck
{
    using Json = nlohmann::json;
    using TreeGraft::PlayerRules;

    inline constexpr int kExitOk = 0;
    inline constexpr int kExitFailed = 1;
    inline constexpr int kExitUsage = 2;

    struct Checker
    {
        int failed = 0;
        void Check(bool ok, const std::string& what)
        {
            if (!ok) ++failed;
            std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        }
    };

    // ------------------------------------------------------------------------
    // Made-up scans and trees
    // ------------------------------------------------------------------------

    /// A scan spell the tree would take, from a plugin, with its traits
    inline Json Spell(const std::string& formId, const std::string& plugin, const std::string& local, const std::string& name,
        const std::string& school, const std::string& level, const std::vector<std::string>& traits = {})
    {
        Json spell = { { "formId", formId }, { "persistentId", plugin + "|" + local }, { "plugin", plugin },
            { "name", name }, { "editorId", name }, { "school", school }, { "skillLevel", level },
            { "taughtByTome", true }, { "traits", traits } };
        return spell;
    }

    /// A saved node as the panel writes it (classicTreeOutput.js)
    inline Json Node(const std::string& formId, const std::string& persistentId, int tier, double x, double y,
        const std::vector<std::string>& children, const std::vector<std::string>& prereqs)
    {
        Json node = { { "formId", formId }, { "children", children }, { "prerequisites", prereqs },
            { "hardPrereqs", Json::array() }, { "softPrereqs", prereqs },
            { "softNeeded", prereqs.empty() ? 0 : 1 }, { "tier", tier }, { "x", x }, { "y", y } };
        if (!persistentId.empty()) node["persistentId"] = persistentId;
        return node;
    }

    inline std::vector<std::string> Names(const std::vector<const Json*>& spells)
    {
        std::vector<std::string> out;
        for (const auto* s : spells) out.push_back(s->value("name", std::string()));
        return out;
    }

    inline std::string Join(const std::vector<std::string>& items)
    {
        std::string out;
        for (const auto& item : items) out += (out.empty() ? "" : ", ") + item;
        return out;
    }

    /// Every node of the tree by "school/formId"
    inline std::map<std::string, Json> NodesOf(const Json& tree)
    {
        std::map<std::string, Json> out;
        for (const auto& [school, data] : tree["schools"].items()) {
            for (const auto& node : data["nodes"]) out[school + "/" + node["formId"].get<std::string>()] = node;
        }
        return out;
    }

    /// Every node of `before` is in `after` with every field as it was, except that a parent's
    /// children may have gained grafted ids at the end. Returns how many nodes gained a child.
    inline bool ExistingKept(const Json& before, const Json& after, const std::set<std::string>& grafted, int& parentsGained)
    {
        bool ok = true;
        parentsGained = 0;
        const auto now = NodesOf(after);
        for (const auto& [key, was] : NodesOf(before)) {
            const auto it = now.find(key);
            if (it == now.end()) {
                std::printf("      %s is gone\n", key.c_str());
                ok = false;
                continue;
            }
            Json a = was;
            Json b = it->second;
            const auto wasChildren = a.value("children", Json::array());
            const auto nowChildren = b.value("children", Json::array());
            a.erase("children");
            b.erase("children");
            bool childrenOk = nowChildren.size() >= wasChildren.size();
            for (std::size_t i = 0; childrenOk && i < nowChildren.size(); ++i) {
                childrenOk = i < wasChildren.size() ? nowChildren[i] == wasChildren[i]
                                                    : grafted.contains(nowChildren[i].get<std::string>());
            }
            if (nowChildren.size() > wasChildren.size()) ++parentsGained;
            if (a != b || !childrenOk) {
                std::printf("      %s changed: %s -> %s\n", key.c_str(), was.dump().c_str(), it->second.dump().c_str());
                ok = false;
            }
        }
        for (const auto& [school, data] : before["schools"].items()) {
            Json a = data;
            Json b = after["schools"][school];
            a.erase("nodes");
            b.erase("nodes");
            if (a != b) {
                std::printf("      school %s fields changed\n", school.c_str());
                ok = false;
            }
        }
        return ok;
    }

    /// The grafted node with this formId, or null
    inline const Json* GraftedNode(const Json& tree, const std::string& school, const std::string& formId)
    {
        for (const auto& node : tree["schools"][school]["nodes"]) {
            if (node["formId"] == formId) return &node;
        }
        return nullptr;
    }

    inline std::vector<const Json*> Pointers(const std::vector<Json>& spells)
    {
        std::vector<const Json*> out;
        for (const auto& s : spells) out.push_back(&s);
        return out;
    }

    /// The grafted nodes (formIds in `grafted`) of `school` under a parent of a higher tier
    inline int TierInversions(const Json& tree, const std::string& school, const std::set<std::string>& grafted)
    {
        std::map<std::string, int> tierOf;
        for (const auto& node : tree["schools"][school]["nodes"]) {
            tierOf[node["formId"].get<std::string>()] = node["tier"].get<int>();
        }
        int inverted = 0;
        for (const auto& node : tree["schools"][school]["nodes"]) {
            const auto id = node["formId"].get<std::string>();
            if (!grafted.contains(id) || node["prerequisites"].empty()) continue;
            const auto parent = node["prerequisites"][0].get<std::string>();
            if (tierOf[parent] <= node["tier"].get<int>()) continue;
            constexpr int kShown = 3;
            if (inverted++ < kShown) {
                std::printf("      %s tier %d under %s tier %d\n", id.c_str(), node["tier"].get<int>(), parent.c_str(),
                    tierOf[parent]);
            }
        }
        return inverted;
    }

    // A small Destruction tree: Flames (root, fire), Firebolt (fire) and Ice Spike (frost) under it.
    // Positions on a spoke along +x from the globe at 0,0, branches above and below.
    inline Json SmallTree()
    {
        return Json{ { "version", "1.0" }, { "layoutMode", "sun" }, { "globe", { { "x", 0 }, { "y", 0 }, { "radius", 45 } } },
            { "schools",
                { { "Destruction",
                      { { "root", "0x00012FCD" }, { "layoutStyle", "tier_first" }, { "spokeAngle", 0 },
                          { "nodes",
                              { Node("0x00012FCD", "Skyrim.esm|0x012FCD", 1, 100, 0, { "0x0001C789", "0x0002B96C" }, {}),
                                  Node("0x0001C789", "Skyrim.esm|0x01C789", 2, 250, 120, {}, { "0x00012FCD" }),
                                  Node("0x0002B96C", "Skyrim.esm|0x02B96C", 2, 250, -120, {}, { "0x00012FCD" }) } } } } } } };
    }

    inline std::vector<Json> SmallScan()
    {
        return { Spell("0x00012FCD", "Skyrim.esm", "0x012FCD", "Flames", "Destruction", "Novice", { "element.fire", "kind.damage" }),
            Spell("0x0001C789", "Skyrim.esm", "0x01C789", "Firebolt", "Destruction", "Apprentice", { "element.fire", "kind.damage" }),
            Spell("0x0002B96C", "Skyrim.esm", "0x02B96C", "IceSpike", "Destruction", "Apprentice", { "element.frost", "kind.damage" }) };
    }
}

namespace GraftCheckCases
{
    // graft-check-cases.cpp
    int DeferredCase();
    int OrderedCase();
    int ManyCase();
    int FallbackCase();
}
