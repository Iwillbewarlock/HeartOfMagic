// tree-remap-check - offline check of a saved tree's load order fix
// (plugins/spelllearning/include/TreeRemap.h, run by SpellScanner::ValidateAndFixTree).
//
//   tree-remap-check --self-test     made-up trees and load orders, no game needed
//
// Each case is a saved tree (every link holds the formIds it was saved with) and
// what the game says now: the formId each persistentId resolves to, and which
// formIds a node without one still finds. The fixed tree is checked two ways:
// the case's own expectations (spells kept, counts), and links read back by
// spell name - every kept node's links, locks, bridges and root must name the
// same spells as before, less the ones that went, and no two kept nodes may
// share a formId.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "TreeRemap.h"

#include <cstdio>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using Json = nlohmann::json;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    /// The current load order as the judge sees it
    struct FakeGame
    {
        std::map<std::string, std::string> resolves;  ///< persistentId -> formId now (absent: plugin or record gone)
        std::set<std::string> legacyValid;            ///< formIds a node without a persistentId still finds
    };

    struct Expect
    {
        std::vector<std::string> kept;  ///< spell names left in the tree, in no particular order
        int resolved = 0;
        int invalid = 0;
        int collisions = 0;
        std::vector<std::string> missingPlugins;
    };

    struct Case
    {
        const char* name;
        Json tree;
        FakeGame game;
        Expect expect;
    };

    TreeRemap::NodeVerdict Judge(const FakeGame& game, const Json& node)
    {
        TreeRemap::NodeVerdict verdict;
        const auto saved = node["formId"].get<std::string>();
        const auto pid = node.value("persistentId", std::string());
        if (pid.find('|') != std::string::npos) {
            const auto it = game.resolves.find(pid);
            if (it == game.resolves.end()) {
                verdict.missingPlugin = pid.substr(0, pid.find('|'));
                return verdict;
            }
            verdict.keep = true;
            verdict.formId = it->second;
            verdict.fromPersistentId = true;
            return verdict;
        }
        verdict.keep = game.legacyValid.contains(saved);
        verdict.formId = saved;
        return verdict;
    }

    Json Node(const char* name, const char* formId, const char* pid, Json links = Json::object())
    {
        Json node = { { "name", name } };
        if (formId) node["formId"] = formId;  // nullptr: a node without one
        if (pid) node["persistentId"] = pid;
        for (auto& [key, value] : links.items()) node[key] = value;
        return node;
    }

    // ------------------------------------------------------------------------
    // Reading links back by spell name
    // ------------------------------------------------------------------------

    using NameOf = std::map<std::string, std::string>;  // formId -> spell name

    struct Snapshot
    {
        NameOf names;                                       // formId -> name
        std::map<std::string, Json> nodes;                  // name -> node
        std::map<std::string, std::string> roots;           // school -> root name (or raw id)
        std::vector<std::pair<std::string, std::string>> bridges;
        std::vector<std::string> duplicateIds;
    };

    std::string NameFor(const NameOf& names, const std::string& id)
    {
        const auto it = names.find(id);
        return it != names.end() ? it->second : "raw:" + id;
    }

    Snapshot Take(const Json& tree)
    {
        Snapshot snap;
        for (const auto& [school, data] : tree["schools"].items()) {
            for (const auto& node : data["nodes"]) {
                if (!node.contains("formId") || !node["formId"].is_string()) continue;
                const auto id = node["formId"].get<std::string>();
                if (!snap.names.emplace(id, node["name"].get<std::string>()).second) snap.duplicateIds.push_back(id);
                snap.nodes[node["name"].get<std::string>()] = node;
            }
        }
        for (const auto& [school, data] : tree["schools"].items()) {
            if (data.contains("root")) snap.roots[school] = NameFor(snap.names, data["root"].get<std::string>());
        }
        if (tree.contains("bridges")) {
            for (const auto& b : tree["bridges"]) {
                snap.bridges.emplace_back(NameFor(snap.names, b["from"].get<std::string>()),
                    NameFor(snap.names, b["to"].get<std::string>()));
            }
        }
        return snap;
    }

    /// One node's links (lists and locks) as names; `keep` filters names of spells that went
    std::vector<std::string> LinkNames(const Json& node, const NameOf& names, const std::set<std::string>* keep)
    {
        std::vector<std::string> out;
        const auto add = [&](const std::string& key, const std::string& id) {
            const auto name = NameFor(names, id);
            if (keep && name.rfind("raw:", 0) != 0 && !keep->contains(name)) return;
            out.push_back(key + "=" + name);
        };
        for (const char* key : TreeRemap::kLinkLists) {
            if (!node.contains(key)) continue;
            for (const auto& link : node[key]) add(key, link.get<std::string>());
        }
        if (node.contains("locks")) {
            for (const auto& lock : node["locks"]) add("lock", lock["nodeId"].get<std::string>());
        }
        return out;
    }

    // ------------------------------------------------------------------------
    // Checks
    // ------------------------------------------------------------------------

    struct Checker
    {
        int failed = 0;
        void Check(bool ok, const std::string& what)
        {
            if (!ok) ++failed;
            std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        }
    };

    std::string Join(const std::vector<std::string>& items)
    {
        std::string out;
        for (const auto& item : items) out += (out.empty() ? "" : ", ") + item;
        return out;
    }

    int RunCase(const Case& c)
    {
        std::printf("  %s\n", c.name);
        Checker check;
        Json tree = c.tree;
        const Snapshot before = Take(tree);
        const auto report = TreeRemap::FixTree(tree, [&c](const Json& node) { return Judge(c.game, node); });
        const Snapshot after = Take(tree);

        std::set<std::string> keptNames;
        for (const auto& [name, node] : after.nodes) keptNames.insert(name);
        const std::set<std::string> expectKept(c.expect.kept.begin(), c.expect.kept.end());
        check.Check(keptNames == expectKept, "spells kept: " + Join({ keptNames.begin(), keptNames.end() }));
        check.Check(after.duplicateIds.empty(), "no two kept nodes share a formId");
        check.Check(report.resolvedFromPersistent == c.expect.resolved && report.invalidNodes == c.expect.invalid &&
                        report.legacyCollisions == c.expect.collisions,
            "counts: " + std::to_string(report.resolvedFromPersistent) + " resolved, " +
                std::to_string(report.invalidNodes) + " invalid, " + std::to_string(report.legacyCollisions) +
                " legacy collisions");
        const std::vector<std::string> missing(report.missingPlugins.begin(), report.missingPlugins.end());
        check.Check(missing == c.expect.missingPlugins, "missing plugins: [" + Join(missing) + "]");

        // Every kept spell's links name the spells they named when saved, less the ones that went
        bool linksOk = true;
        for (const auto& name : keptNames) {
            const auto was = LinkNames(before.nodes.at(name), before.names, &keptNames);
            const auto now = LinkNames(after.nodes.at(name), after.names, nullptr);
            if (was != now) {
                linksOk = false;
                std::printf("      %s: saved [%s], now [%s]\n", name.c_str(), Join(was).c_str(), Join(now).c_str());
            }
        }
        check.Check(linksOk, "every link, lock and prerequisite still names its own spell");

        std::vector<std::pair<std::string, std::string>> bridgesWas;
        for (const auto& b : before.bridges) {
            if (keptNames.contains(b.first) && keptNames.contains(b.second)) bridgesWas.push_back(b);
        }
        check.Check(after.bridges == bridgesWas, "bridges follow their spells; one to a spell that went is dropped");

        bool rootsOk = true;
        for (const auto& [school, rootWas] : before.roots) {
            const auto& nodes = tree["schools"][school]["nodes"];
            const std::string expectRoot = keptNames.contains(rootWas) ? rootWas
                                           : nodes.empty()             ? rootWas
                                                                       : nodes[0]["name"].get<std::string>();
            const auto it = after.roots.find(school);
            const std::string rootNow = it != after.roots.end() ? it->second : "";
            if (rootNow != expectRoot && !(nodes.empty() && rootNow.rfind("raw:", 0) == 0)) {
                rootsOk = false;
                std::printf("      %s root: %s, expected %s\n", school.c_str(), rootNow.c_str(), expectRoot.c_str());
            }
        }
        check.Check(rootsOk, "school roots: a moved root follows, a root that went gives way to the first spell left");
        return check.failed;
    }

    // ------------------------------------------------------------------------
    // The cases
    // ------------------------------------------------------------------------

    std::vector<Case> Cases()
    {
        std::vector<Case> cases;

        // Light plugin A taken out: B and C each move down one light index. Every new
        // formId of B and C is the saved formId of the node before it
        cases.push_back({ "shift: light plugin A removed, B and C move down one index",
            { { "schools",
                  { { "Destruction",
                      { { "root", "0xFE906800" },
                          { "nodes",
                              Json::array({
                                  Node("Alpha", "0xFE905800", "A.esl|0x000800", { { "prerequisites", { "0xFE906800" } } }),
                                  Node("Bravo", "0xFE906800", "B.esl|0x000800",
                                      { { "children", { "0xFE907800", "0xFE905800" } } }),
                                  Node("Charlie", "0xFE907800", "C.esl|0x000800",
                                      { { "prerequisites", { "0xFE906800" } }, { "hardPrereqs", { "0xFE906800" } },
                                          { "softPrereqs", { "0xFE905800" } },
                                          { "locks", Json::array({ { { "nodeId", "0xFE906800" }, { "type", "x" } } }) } }),
                                  Node("Delta", "0x01000D62", "Skyrim.esm|0x000D62", { { "children", { "0xFE907800" } } }),
                              }) } } } } },
                { "bridges", Json::array({ { { "from", "0xFE906800" }, { "to", "0x01000D62" } },
                                 { { "from", "0xFE905800" }, { "to", "0x01000D62" } } }) } },
            { { { "B.esl|0x000800", "0xFE905800" }, { "C.esl|0x000800", "0xFE906800" },
                  { "Skyrim.esm|0x000D62", "0x01000D62" } },
                {} },
            { { "Bravo", "Charlie", "Delta" }, 2, 1, 0, { "A.esl" } } });

        // Two light plugins swap indices: each new formId is the other's saved one
        cases.push_back({ "swap: Arclight and Bloodmoon trade light indices",
            { { "schools",
                  { { "Destruction",
                      { { "root", "0xFE906838" },
                          { "nodes",
                              Json::array({
                                  Node("Arclight", "0xFE906838", "Arclight.esp|0x000838",
                                      { { "children", { "0x02000801" } }, { "softPrereqs", { "0xFE907838" } } }),
                                  Node("ArcChild", "0x02000801", "Vanilla.esm|0x000801",
                                      { { "prerequisites", { "0xFE906838" } } }),
                              }) } } },
                      { "Conjuration",
                          { { "root", "0xFE907838" },
                              { "nodes",
                                  Json::array({
                                      Node("Bloodmoon", "0xFE907838", "Bloodmoon.esp|0x000838",
                                          { { "children", { "0x02000802" } } }),
                                      Node("BloodChild", "0x02000802", "Vanilla.esm|0x000802",
                                          { { "prerequisites", { "0xFE907838" } },
                                              { "locks", Json::array({ { { "nodeId", "0xFE906838" } } }) } }),
                                  }) } } } } },
                { "bridges", Json::array({ { { "from", "0xFE906838" }, { "to", "0xFE907838" } } }) } },
            { { { "Arclight.esp|0x000838", "0xFE907838" }, { "Bloodmoon.esp|0x000838", "0xFE906838" },
                  { "Vanilla.esm|0x000801", "0x02000801" }, { "Vanilla.esm|0x000802", "0x02000802" } },
                {} },
            { { "Arclight", "ArcChild", "Bloodmoon", "BloodChild" }, 2, 0, 0, {} } });

        // The 2026-10-05 report: Arclight's spell went and Bloodmoon's took its old formId
        cases.push_back({ "slot taken: Arclight's spell gone, Bloodmoon's moved onto its saved formId",
            { { "schools",
                  { { "Destruction",
                      { { "root", "0xFE906838" },
                          { "nodes",
                              Json::array({
                                  Node("Arclight", "0xFE906838", "Arclight.esp|0x000838", { { "children", { "0xFE907838" } } }),
                                  Node("Bloodmoon", "0xFE907838", "Bloodmoon.esp|0x000838",
                                      { { "prerequisites", { "0xFE906838" } }, { "children", { "0x02000803" } } }),
                                  Node("Echo", "0x02000803", "Vanilla.esm|0x000803",
                                      { { "prerequisites", { "0xFE907838" } }, { "softPrereqs", { "0xFE906838" } } }),
                              }) } } } } } },
            { { { "Bloodmoon.esp|0x000838", "0xFE906838" }, { "Vanilla.esm|0x000803", "0x02000803" } }, {} },
            { { "Bloodmoon", "Echo" }, 1, 1, 0, { "Arclight.esp" } } });

        // Nodes without a persistentId (a tree from before they were saved)
        cases.push_back({ "legacy nodes: one still found, one gone, one on a resolved spell's formId",
            { { "schools",
                  { { "Alteration",
                      { { "root", "0xFE905800" },
                          { "nodes",
                              Json::array({
                                  // Its plugin left; a resolved spell moved onto its formId
                                  Node("OldA", "0xFE905800", nullptr, { { "children", { "0x03000900" } } }),
                                  Node("Moved", "0xFE906800", "B.esl|0x000800", { { "children", { "0xFE905800" } } }),
                                  Node("Stays", "0x03000900", nullptr,
                                      { { "prerequisites", { "0xFE905800", "0xFE906800" } },
                                          { "children", { "0x03000901" } } }),
                                  Node("Gone", "0x03000901", nullptr, { { "prerequisites", { "0x03000900" } } }),
                                  Node("NoFormId", nullptr, nullptr),
                              }) } } } } } },
            { { { "B.esl|0x000800", "0xFE905800" } }, { "0xFE905800", "0x03000900" } },
            { { "Moved", "Stays" }, 1, 3, 1, {} } });

        // Links to spells that went: a removed plugin, a record gone from a loaded one
        cases.push_back({ "links to removed spells: plugin gone, record gone, link to no node",
            { { "schools",
                  { { "Restoration",
                      { { "root", "0x05000100" },
                          { "nodes",
                              Json::array({
                                  Node("RemovedRoot", "0x05000100", "Gone.esp|0x000100",
                                      { { "children", { "0x01000200", "0x01000300" } } }),
                                  Node("Heal", "0x01000200", "Kept.esp|0x000200",
                                      { { "prerequisites", { "0x05000100" } }, { "children", { "0x01000300" } },
                                          { "softPrereqs", { "0x0A00ABCD" } },
                                          { "locks", Json::array({ { { "nodeId", "0x05000100" } },
                                                         { { "nodeId", "0x01000300" } } }) } }),
                                  Node("RecordGone", "0x01000300", "Kept.esp|0x000300",
                                      { { "prerequisites", { "0x01000200" } } }),
                              }) } } } } },
                { "bridges", Json::array({ { { "from", "0x01000200" }, { "to", "0x05000100" } },
                                 { { "from", "0x01000200" }, { "to", "0x01000200" } } }) } },
            { { { "Kept.esp|0x000200", "0x02000200" } }, {} },
            { { "Heal" }, 1, 2, 0, { "Gone.esp", "Kept.esp" } } });

        // Nothing moved: the tree comes out as it went in
        cases.push_back({ "same load order: nothing changes",
            { { "schools",
                  { { "Illusion",
                      { { "root", "0x01000010" },
                          { "nodes",
                              Json::array({
                                  Node("Calm", "0x01000010", "Skyrim.esm|0x000010", { { "children", { "0x01000011" } } }),
                                  Node("Fury", "0x01000011", "Skyrim.esm|0x000011", { { "prerequisites", { "0x01000010" } } }),
                              }) } } } } } },
            { { { "Skyrim.esm|0x000010", "0x01000010" }, { "Skyrim.esm|0x000011", "0x01000011" } }, {} },
            { { "Calm", "Fury" }, 0, 0, 0, {} } });

        return cases;
    }

    int SelfTest()
    {
        int failedCases = 0;
        const auto cases = Cases();
        for (const auto& c : cases) {
            if (RunCase(c) > 0) ++failedCases;
        }
        std::printf("self-test: %d of %zu cases failed\n", failedCases, cases.size());
        return failedCases == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: tree-remap-check --self-test\n");
        return kExitUsage;
    }
    return SelfTest();
}
