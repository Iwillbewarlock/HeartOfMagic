// graft-check, its placement cases (graft-check.cpp runs them): a formId the file already
// holds deferred, the file's key order kept, 300 spells at once, a parent past the cap but
// never of a higher tier.

#include "graft-check.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace GraftCheck;

namespace GraftCheckCases
{
    int DeferredCase()
    {
        std::printf("  graft defers a formId the file already holds\n");
        Checker check;
        // Destruction: a live node whose saved formId is stale (its record now has 0x06000900) and a
        // removed plugin's node; Restoration: one live root
        Json tree = SmallTree();
        auto& nodes = tree["schools"]["Destruction"]["nodes"];
        nodes.push_back(Node("0x05000900", "Moved.esp|0x000900", 2, 250, 0, {}, { "0x00012FCD" }));
        nodes.push_back(Node("0x0E000600", "Gone.esp|0x000600", 2, 250, 40, {}, { "0x00012FCD" }));
        tree["schools"]["Restoration"] = { { "root", "0x00012FCC" },
            { "nodes", { Node("0x00012FCC", "Skyrim.esm|0x012FCC", 1, -100, 0, {}, {}) } } };
        auto scan = SmallScan();
        scan.push_back(Spell("0x06000900", "Moved.esp", "0x000900", "Moved", "Destruction", "Apprentice", { "element.fire" }));
        scan.push_back(Spell("0x00012FCC", "Skyrim.esm", "0x012FCC", "Healing", "Restoration", "Novice"));
        Json noPersistent = Spell("0x05000a10", "New.esp", "0x000A10", "NoPid", "Destruction", "Adept", { "element.fire" });
        noPersistent.erase("persistentId");
        const std::vector<Json> news = {
            Spell("0x05000900", "Took.esp", "0x000900", "SameSchool", "Destruction", "Adept", { "element.fire" }),
            Spell("0x0E000600", "Took.esp", "0x000600", "OtherSchool", "Restoration", "Apprentice"),
            noPersistent,
            Spell("0x05000a11", "New.esp", "0x000A11", "Fine", "Destruction", "Adept", { "element.fire" }),
        };
        const auto result = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, TreeBuilder::BuildConfig{});
        const std::vector<std::string> expectDeferred = { "Took.esp|0x000900", "Took.esp|0x000600", "0x05000a10" };
        check.Check(result.deferred == expectDeferred, "a stale-id clash (same school, other school) and no persistentId "
                                                       "are deferred: " + Join(result.deferred));
        check.Check(result.leftOut.empty() && result.skippedNoSchool.empty() &&
                        result.grafted == std::vector<std::string>{ "New.esp|0x000A11" },
            "deferred spells are neither left out nor grafted; the clean one is grafted");
        std::map<std::string, int> idCount;
        for (const auto& [school, data] : result.tree["schools"].items()) {
            for (const auto& node : data["nodes"]) ++idCount[node["formId"].get<std::string>()];
        }
        check.Check(std::ranges::all_of(idCount, [](const auto& entry) { return entry.second == 1; }),
            "no saved formId twice in the output");
        bool noEmptyPersistent = true;
        for (const auto& node : result.tree["schools"]["Destruction"]["nodes"]) {
            noEmptyPersistent = noEmptyPersistent && !(node.contains("persistentId") && node["persistentId"] == "");
        }
        check.Check(noEmptyPersistent, "no node is written with an empty persistentId");
        int parentsGained = 0;
        check.Check(ExistingKept(tree, result.tree, { "0x05000a11" }, parentsGained) && parentsGained == 1,
            "existing nodes unchanged, one parent gained the clean spell");
        return check.failed;
    }

    int OrderedCase()
    {
        std::printf("  graft keeps the file's key order\n");
        Checker check;
        nlohmann::ordered_json tree = nlohmann::ordered_json::parse(SmallTree().dump());
        auto restoration = nlohmann::ordered_json::parse(
            R"({"root":"0x00012FCC","nodes":[{"formId":"0x00012FCC","children":[],"prerequisites":[],"tier":1,"x":-100,"y":0}]})");
        nlohmann::ordered_json schools;
        schools["Restoration"] = restoration;
        schools["Destruction"] = tree["schools"]["Destruction"];
        tree["schools"] = schools;
        auto scan = SmallScan();
        scan.push_back(Spell("0x00012FCC", "Skyrim.esm", "0x012FCC", "Healing", "Restoration", "Novice"));
        const std::vector<Json> news = { Spell("0x05000801", "Pyro.esp", "0x000801", "Fireball", "Destruction", "Adept",
            { "element.fire" }) };
        const auto result = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, TreeBuilder::BuildConfig{});
        check.Check(result.tree["schools"].begin().key() == "Restoration", "schools stay in the file's order");
        check.Check(result.grafted.size() == 1, "and the spell is grafted");
        return check.failed;
    }

    int ManyCase()
    {
        std::printf("  graft: 300 new spells\n");
        Checker check;
        // A Destruction tree of 31 nodes: a root and 3 branches (fire, frost, shock) of 10, tiers 2-5
        const char* elements[] = { "element.fire", "element.frost", "element.shock" };
        Json nodes = Json::array();
        std::vector<Json> scan;
        nodes.push_back(Node("0x00012FCD", "Skyrim.esm|0x012FCD", 1, 100, 0, {}, {}));
        scan.push_back(Spell("0x00012FCD", "Skyrim.esm", "0x012FCD", "Flames", "Destruction", "Novice", { "element.fire" }));
        const char* levels[] = { "Novice", "Apprentice", "Adept", "Expert", "Master" };
        for (int b = 0; b < 3; ++b) {
            std::string parent = "0x00012FCD";
            for (int i = 0; i < 10; ++i) {
                const int tier = 2 + i * 4 / 10;   // 2,2,2,3,3,4,4,4,5,5
                const auto id = std::format("0x0001{:04X}", b * 0x100 + i);
                const double angle = (b - 1) * 0.5;
                const double radius = 100 + 90 * (i + 1);
                Json node = Node(id, std::format("Skyrim.esm|0x01{:04X}", b * 0x100 + i), tier, radius * std::cos(angle),
                    radius * std::sin(angle) + i * 7, {}, { parent });
                nodes.push_back(node);
                for (auto& n : nodes) {
                    if (n["formId"] == parent) n["children"].push_back(id);
                }
                scan.push_back(Spell(id, "Skyrim.esm", std::format("0x01{:04X}", b * 0x100 + i), std::format("Base{}_{}", b, i),
                    "Destruction", levels[tier - 1], { elements[b], "kind.damage" }));
                parent = id;
            }
        }
        const Json tree = { { "globe", { { "x", 0 }, { "y", 0 } } },
            { "schools", { { "Destruction", { { "root", "0x00012FCD" }, { "nodes", nodes } } } } } };
        std::vector<Json> news;
        std::set<std::string> newIds;
        for (int i = 0; i < 300; ++i) {
            const auto id = std::format("0x0A{:06X}", 0x800 + i);
            news.push_back(Spell(id, "BigPack.esp", std::format("0x{:06X}", 0x800 + i), std::format("Pack{}", i), "Destruction",
                levels[i % 5], { elements[i % 3], (i % 2) ? "kind.damage" : "kind.cloak" }));
            newIds.insert(id);
        }
        const auto start = std::chrono::steady_clock::now();
        const auto result = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, TreeBuilder::BuildConfig{});
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("    (300 spells grafted in %.0f ms)\n", ms);
        check.Check(result.grafted.size() == 300, std::format("graft: 300 new spells - completes ({} grafted)", result.grafted.size()));
        int parentsGained = 0;
        check.Check(ExistingKept(tree, result.tree, newIds, parentsGained), "existing nodes unchanged");

        std::map<std::string, Json> byId;
        for (const auto& node : result.tree["schools"]["Destruction"]["nodes"]) byId[node["formId"].get<std::string>()] = node;
        bool parented = true;
        int maxChildren = 0;
        for (const auto& id : newIds) {
            const auto it = byId.find(id);
            if (it == byId.end()) {
                parented = false;
                continue;
            }
            const auto& prereqs = it->second["prerequisites"];
            const auto parent = prereqs.size() == 1 ? byId.find(prereqs[0].get<std::string>()) : byId.end();
            const bool linked = parent != byId.end() && std::ranges::find(parent->second["children"], Json(id)) !=
                                                            parent->second["children"].end();
            if (!linked) std::printf("      %s has no parent in its school: %s\n", id.c_str(), prereqs.dump().c_str());
            parented = parented && linked;
        }
        for (const auto& [id, node] : byId) maxChildren = (std::max)(maxChildren, static_cast<int>(node["children"].size()));
        check.Check(parented, "each grafted node has a parent in its school");
        // A parent takes up to kFallbackExtraChildren past the cap when no parent of the spell's tier or below has room
        check.Check(maxChildren <= TreeBuilder::BuildConfig{}.maxChildrenPerNode + TreeBuilder::kFallbackExtraChildren,
            std::format("no node over the child cap and its fallback overflow ({} at most)", maxChildren));
        check.Check(TierInversions(result.tree, "Destruction", newIds) == 0,
            "graft: 300 new spells - no grafted node under a parent of a higher tier");
        return check.failed;
    }

    /// Root (Novice) -> three Apprentice nodes -> three Adept nodes each, all at the cap of 3;
    /// with `adeptsFull` each Adept node has three Expert children too
    Json FullTree(bool adeptsFull, std::vector<Json>& scan)
    {
        const char* levels[] = { "Novice", "Apprentice", "Adept", "Expert" };
        const auto id = [](int i) { return std::format("0x050{:05X}", i); };
        const auto local = [](int i) { return std::format("0x{:06X}", i); };
        Json nodes = Json::array();
        const auto add = [&](int i, int tier, double x, double y, const std::vector<std::string>& children,
                             const std::vector<std::string>& prereqs) {
            nodes.push_back(Node(id(i), "Full.esp|" + local(i), tier, x, y, children, prereqs));
            scan.push_back(Spell(id(i), "Full.esp", local(i), std::format("Full{}", i), "Destruction", levels[tier - 1],
                { "element.fire" }));
        };
        constexpr int kWidth = 3;
        constexpr int kFirstLeaf = 100;
        add(0, 1, 100, 0, { id(1), id(2), id(3) }, {});
        int next = kWidth + 1;
        int leaf = kFirstLeaf;
        for (int a = 1; a <= kWidth; ++a) {
            std::vector<std::string> kids;
            for (int k = 0; k < kWidth; ++k) kids.push_back(id(next + k));
            add(a, 2, 200, (a - 2) * 100.0, kids, { id(0) });
            for (int k = 0; k < kWidth; ++k, ++next) {
                std::vector<std::string> leaves;
                for (int l = 0; adeptsFull && l < kWidth; ++l) leaves.push_back(id(leaf + l));
                const double y = (a - 2) * 100.0 + (k - 1) * 30.0;
                add(next, 3, 300, y, leaves, { id(a) });
                for (int l = 0; adeptsFull && l < kWidth; ++l, ++leaf) add(leaf, 4, 400, y + (l - 1) * 9.0, {}, { id(next) });
            }
        }
        return { { "globe", { { "x", 0 }, { "y", 0 } } },
            { "schools", { { "Destruction", { { "root", id(0) }, { "nodes", nodes } } } } } };
    }

    int FallbackCase()
    {
        std::printf("  graft: no parent with room\n");
        Checker check;
        // Root and Apprentice nodes full, the Adept nodes free: a new Novice and Apprentice
        // spell still go under their own tier or below, past the cap
        std::vector<Json> scan;
        const Json tree = FullTree(false, scan);
        const std::vector<Json> news = {
            Spell("0x06000001", "New.esp", "0x000001", "NewNovice", "Destruction", "Novice", { "element.fire" }),
            Spell("0x06000002", "New.esp", "0x000002", "NewApprentice", "Destruction", "Apprentice", { "element.fire" }) };
        const auto result = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, TreeBuilder::BuildConfig{});
        check.Check(result.grafted.size() == 2, std::format("graft: no parent with room - both grafted ({})", result.grafted.size()));
        check.Check(TierInversions(result.tree, "Destruction", { "0x06000001", "0x06000002" }) == 0,
            "graft: no parent with room - neither under a free Adept node (a higher tier)");
        const Json* novice = GraftedNode(result.tree, "Destruction", "0x06000001");
        check.Check(novice && (*novice)["prerequisites"] == Json::array({ "0x05000000" }),
            "the Novice spell goes under the full root (its tier, past the cap)");

        // Every node full: the Novice still goes under the root, nothing is skipped
        std::vector<Json> fullScan;
        const Json full = FullTree(true, fullScan);
        const std::vector<Json> one = { news[0] };
        const auto all = TreeGraft::Graft(full, fullScan, Pointers(one), PlayerRules{}, TreeBuilder::BuildConfig{});
        const Json* root = GraftedNode(all.tree, "Destruction", "0x05000000");
        check.Check(all.grafted.size() == 1 && all.skippedNoSchool.empty() && root && (*root)["children"].size() == 4,
            "graft: root at the cap and every node full - the Novice goes under the root");
        int parentsGained = 0;
        check.Check(ExistingKept(full, all.tree, { "0x06000001" }, parentsGained) && parentsGained == 1,
            "existing nodes unchanged but the root's new child");
        return check.failed;
    }
}
