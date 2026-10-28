// graft-run-check - offline check of the graft's runner at the main menu
// (plugins/spelllearning/include/uimanager/TreeGraftRun.h, wired by TreeGraftRun.cpp).
//
//   graft-run-check --self-test     made-up trees, scans, records and configs, no game needed
//
// When it runs (the tree record's load order not the current one, the catalog the
// current load order's full scan, a scan that marked everything with the tome reach
// pass ready); the player's rules read from config.json in the panel's key formats;
// the seen list (the record's, else the previous catalog's spells, else none: nothing
// grafted) and the record a graft leaves; the stale formIds of live nodes refreshed
// from what validation says (a removed plugin's node and its id left as they are);
// the declutter with every existing node fixed; and the verification the dev
// self-test reports (every pre-graft node unchanged but for ids and a new child, each
// grafted node under a live parent of its school, no seen spell grafted). The last
// case runs the whole plan: a live node saved under a stale formId that a new spell
// now has - refreshed, so the new spell is grafted, not deferred. And the review's
// fixes: the scan dump is the record's, the seen list kept before the catalog is
// replaced, and the journal that keeps the record with the tree.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "Common.h"

#include "TreeRemap.h"
#include "uimanager/TreeGraftRun.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// graft-run-check-record.cpp: the scan dump, the seen list kept, the journal
namespace GraftRunRecordCases
{
    int DumpCase();
    int SeedCase();
    int JournalCase();
    int LostCase();
}

namespace
{
    using Json = nlohmann::json;
    using OrderedJson = nlohmann::ordered_json;
    using TreeRecordFile::TreeRecord;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

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
    // Made-up data
    // ------------------------------------------------------------------------

    Json Spell(const std::string& formId, const std::string& plugin, const std::string& local, const std::string& name,
        const std::string& school, const std::string& level, const std::vector<std::string>& traits = {})
    {
        return { { "formId", formId }, { "persistentId", plugin + "|" + local }, { "plugin", plugin }, { "name", name },
            { "editorId", name }, { "school", school }, { "skillLevel", level }, { "taughtByTome", true },
            { "traits", traits } };
    }

    OrderedJson Node(const std::string& formId, const std::string& persistentId, int tier, double x, double y,
        const std::vector<std::string>& children, const std::vector<std::string>& prereqs)
    {
        OrderedJson node = { { "formId", formId }, { "children", children }, { "prerequisites", prereqs },
            { "hardPrereqs", OrderedJson::array() }, { "softPrereqs", prereqs }, { "softNeeded", prereqs.empty() ? 0 : 1 },
            { "tier", tier }, { "x", x }, { "y", y } };
        if (!persistentId.empty()) node["persistentId"] = persistentId;
        return node;
    }

    /// What the game says now, for TreeRemap's own FixTree (as SpellScanner::ValidateAndFixTree runs it)
    struct FakeGame
    {
        std::map<std::string, std::string> resolves;                          ///< persistentId -> formId now
        std::map<std::string, std::pair<std::string, std::string>> rematch;   ///< persistentId -> (formId, new persistentId)
        std::set<std::string> legacyLive;                                     ///< formIds a node without a persistentId finds
        std::set<std::string> notPlayerSpells;                                ///< persistentIds dropped as no player's spell

        TreeRemap::NodeVerdict operator()(const Json& node, const std::string&) const
        {
            TreeRemap::NodeVerdict verdict;
            verdict.formId = node["formId"].get<std::string>();
            const std::string persistentId = node.value("persistentId", std::string());
            if (persistentId.find('|') != std::string::npos) {
                verdict.hasPersistentId = true;
                if (const auto it = resolves.find(persistentId); it != resolves.end()) {
                    verdict.formId = it->second;
                    verdict.keep = !notPlayerSpells.contains(persistentId);
                } else if (const auto found = rematch.find(persistentId); found != rematch.end()) {
                    verdict.formId = found->second.first;
                    verdict.rematched = true;
                    verdict.persistentId = found->second.second;
                    verdict.keep = true;
                } else {
                    verdict.missingPlugin = persistentId.substr(0, persistentId.find('|'));
                }
                return verdict;
            }
            verdict.keep = legacyLive.contains(TreeRemap::NormalizeFormId(verdict.formId));
            return verdict;
        }
    };

    /// The verdicts the runner gets back from its game thread validation of the tagged copy
    std::vector<TreeGraftRun::NodeVerdict> Validate(const OrderedJson& tree, const FakeGame& game)
    {
        std::size_t tagged = 0;
        Json copy = TreeGraftRun::TaggedCopy(tree, tagged);
        TreeRemap::FixTree(copy, game);
        return TreeGraftRun::VerdictsOf(copy, tagged);
    }

    const OrderedJson& NodeAt(const OrderedJson& tree, const char* school, std::size_t i)
    {
        return tree["schools"][school]["nodes"][i];
    }

    // ------------------------------------------------------------------------
    // Cases
    // ------------------------------------------------------------------------

    int TriggerCase()
    {
        std::printf("trigger\n");
        Checker check;
        TreeGraftRun::Facts facts;
        facts.loadOrder = "new";
        facts.recordLoadOrder = "old";
        facts.treeHasSpells = true;
        facts.catalogMatches = true;
        check.Check(!TreeGraftRun::WhyNotRun(facts), "runs: record of another load order, catalog this one's");
        auto same = facts;
        same.recordLoadOrder = "new";
        check.Check(TreeGraftRun::WhyNotRun(same).has_value(), "not: the record is this load order's");
        auto noRecord = facts;
        noRecord.recordLoadOrder.clear();
        check.Check(!TreeGraftRun::WhyNotRun(noRecord), "runs: no record (a tree saved before the record)");
        auto unreadable = facts;
        unreadable.recordUnreadable = true;
        check.Check(TreeGraftRun::WhyNotRun(unreadable).has_value(), "not: the record file is there but unreadable");
        auto stale = facts;
        stale.catalogMatches = false;
        check.Check(TreeGraftRun::WhyNotRun(stale).has_value(),
            "not: the scan on disk is older than the load order (the catalog does not match)");
        auto noTree = facts;
        noTree.treeHasSpells = false;
        check.Check(TreeGraftRun::WhyNotRun(noTree).has_value(), "not: no tree with spells");
        auto unknown = facts;
        unknown.loadOrder.clear();
        check.Check(TreeGraftRun::WhyNotRun(unknown).has_value(), "not: the load order is unknown");

        // AutoScan's skip path (the catalog this load order's): scan when the dump is not that scan's (Ruling 22)
        int asked = 0;
        const auto dump = [&asked](bool matches) { return [&asked, matches] { ++asked; return matches; }; };
        check.Check(TreeGraftRun::ScanForGraft(facts, dump(false)), "scan: a graft due, the dump not the catalog scan's");
        check.Check(!TreeGraftRun::ScanForGraft(facts, dump(true)), "skip: a graft due, the dump the catalog scan's");
        asked = 0;
        check.Check(!TreeGraftRun::ScanForGraft(same, dump(false)) && !TreeGraftRun::ScanForGraft(unreadable, dump(false)) &&
                        asked == 0,
            "skip: no graft due (the record this load order's, or unreadable) - the dump is not read");

        const Json complete = { { "spells", Json::array() } };
        check.Check(!TreeGraftRun::WhyScanUnusable(complete, "ready"), "scan: complete, tome reach ready");
        const Json incomplete = { { "spells", Json::array() }, { "incomplete", { "vampire casters" } } };
        check.Check(TreeGraftRun::WhyScanUnusable(incomplete, "ready").has_value(), "scan: incomplete - not");
        check.Check(TreeGraftRun::WhyScanUnusable(complete, "failed:x").has_value(), "scan: tome reach failed - not");
        check.Check(TreeGraftRun::WhyScanUnusable(Json::object(), "ready").has_value(), "scan: no spells list - not");
        return check.failed;
    }

    int RulesCase()
    {
        std::printf("player rules from config.json\n");
        Checker check;
        const Json config = {
            { "spellBlacklist", { { { "formId", "0xA0055666" }, { "localFormId", "055666" }, { "name", "x" },
                                    { "plugin", "Inigo.esp" }, { "school", "Conjuration" } },
                                  { { "formId", "0x0500080B" } },
                                  { { "plugin", "Light.esl" }, { "localFormId", "80A" }, { "formId", "0xFE001" } },
                                  "not an entry", { { "plugin", "" }, { "localFormId", "" } } } },
            { "gateReviewed", { "mysticismmagic.esp:ff19e0", 7, "LostGrimoire.esp:07AD13" } },
            { "pluginWhitelist", { { { "enabled", true }, { "plugin", "Dawnguard.esm" } },
                                   { { "enabled", false }, { "plugin", "Some MOD.esp" } }, { { "plugin", "NoFlag.esp" } },
                                   { { "enabled", false } } } } };
        const auto rules = TreeGraft::PlayerRulesOf(config);
        check.Check(rules.blacklistKeys.contains("inigo.esp:055666"), "blacklist: plugin + local id, folded and lowered");
        check.Check(rules.blacklistKeys.contains("light.esl:80a"), "blacklist: an ESL's 3-digit local id lowered");
        check.Check(rules.blacklistKeys.contains("0x0500080B"), "blacklist: a formId-only entry as saved");
        check.Check(!rules.blacklistKeys.contains("0xA0055666") && !rules.blacklistKeys.contains("0xFE001"),
            "blacklist: an entry with plugin and local id is not also kept by its formId");
        check.Check(rules.blacklistKeys.size() == 3, std::format("blacklist: nothing else ({} keys)", rules.blacklistKeys.size()));
        check.Check(rules.gateKept.contains("mysticismmagic.esp:ff19e0") && rules.gateKept.contains("LostGrimoire.esp:07AD13") &&
                        rules.gateKept.size() == 2,
            "gateReviewed: the strings as saved, nothing else");
        check.Check(rules.pluginsOff.contains("some mod.esp") && !rules.pluginsOff.contains("dawnguard.esm") &&
                        !rules.pluginsOff.contains("noflag.esp"),
            "whitelist: only enabled false, folded");

        // The keys work with the rule the graft applies (the panel's spell fields)
        Json inigo = Spell("0x1E055666", "Inigo.esp", "0x055666", "Summon Inigo", "Conjuration", "Novice");
        check.Check(!TreeGraft::IsTreeCandidate(inigo, rules), "a blacklisted spell is left out (load order moved)");
        Json other = Spell("0x05000800", "Some MOD.esp", "0x000800", "Spark", "Destruction", "Novice");
        check.Check(!TreeGraft::IsTreeCandidate(other, rules), "a spell of a plugin turned off is left out");
        Json fine = Spell("0x06000800", "Fine.esp", "0x000800", "Ember", "Destruction", "Novice");
        check.Check(TreeGraft::IsTreeCandidate(fine, rules), "any other spell is a candidate");
        check.Check(TreeGraft::PlayerRulesOf(Json::array()).blacklistKeys.empty(), "a config that is not an object: no rules");
        return check.failed;
    }

    int BaselineCase()
    {
        std::printf("seen list baseline\n");
        Checker check;
        TreeRecord record;
        record.hasSeen = true;
        record.seen = { "A.esp|0x000800" };
        const Json catalog = { { "spells", { { "B.esp|0x000801", Json::object() }, { "C.esp|0x000802", Json::object() } } } };
        auto from = TreeGraftRun::BaselineOf(record, std::optional<Json>(catalog));
        check.Check(from.source == TreeGraftRun::SeenSource::kRecord && from.seen == record.seen, "the record's list first");
        record.seen.clear();
        from = TreeGraftRun::BaselineOf(record, std::optional<Json>(catalog));
        check.Check(from.source == TreeGraftRun::SeenSource::kRecord && from.seen.empty(), "an empty list is a list");
        record.hasSeen = false;
        from = TreeGraftRun::BaselineOf(record, std::optional<Json>(catalog));
        check.Check(from.source == TreeGraftRun::SeenSource::kCatalog && from.seen.size() == 2,
            "no list: the previous catalog's spells");
        from = TreeGraftRun::BaselineOf(record, std::nullopt);
        check.Check(from.source == TreeGraftRun::SeenSource::kNone, "no list, no catalog: none (nothing grafted)");
        from = TreeGraftRun::BaselineOf(record, std::optional<Json>(Json{ { "spells", Json::object() } }));
        check.Check(from.source == TreeGraftRun::SeenSource::kNone, "a catalog without spells: none");
        return check.failed;
    }

    int RecordCase()
    {
        std::printf("the record a graft leaves\n");
        Checker check;
        TreeRecord record;
        record.loadOrder = "old";
        record.noticedLoadOrder = "new";
        record.pendingGrafted = 1;
        record.extra = { { "later", 1 } };
        TreeGraftRun::Baseline baseline{ { "A.esp|0x000800" }, TreeGraftRun::SeenSource::kCatalog };
        const auto done = TreeGraftRun::RecordAfterGraft(record, baseline, { "N.esp|0x000800", "a.esp|0x000800" },
            { "L.esp|0x000801" }, true, "new", 10, 20);
        check.Check(done.hasSeen && done.seen == std::vector<std::string>{ "A.esp|0x000800", "N.esp|0x000800", "L.esp|0x000801" },
            "seen = baseline + grafted + left out, once each (A-Z folded)");
        check.Check(done.pendingGrafted == 3, std::format("pendingGrafted adds the grafted ({})", done.pendingGrafted));
        check.Check(done.loadOrder == "new" && done.plugins == 10 && done.spells == 20 && done.noticedLoadOrder.empty(),
            "complete: the record is this load order's, the notice cleared");
        check.Check(done.extra == record.extra, "unknown fields kept");
        const auto held = TreeGraftRun::RecordAfterGraft(record, baseline, { "N.esp|0x000800" }, {}, false, "new", 10, 20);
        check.Check(held.loadOrder == "old" && held.noticedLoadOrder == "new",
            "deferred or unplaced spells: the load order stays (the graft runs again, the old notice applies)");
        return check.failed;
    }

    OrderedJson RefreshTree()
    {
        // A: live, saved under a stale formId; M: a removed plugin's node; H: live, but its formId now is
        // M's saved one (held: two nodes on one id otherwise); L: legacy copy of A in Restoration; X: no formId
        OrderedJson destruction = { { "root", "0x00012FCD" },
            { "nodes", { Node("0x00012FCD", "Skyrim.esm|0x012FCD", 1, 0, 0, { "0x05000801", "0x06000802" }, {}),
                         Node("0x05000801", "ModA.esp|0x000801", 2, 60, 0, {}, { "0x00012FCD" }),
                         Node("0x06000802", "Gone.esp|0x000802", 2, 0, 60, { "0x05000801" }, { "0x00012FCD" }),
                         Node("0x07000803", "ModH.esp|0x000803", 2, -60, 0, {}, { "0x05000801" }),
                         OrderedJson{ { "name", "no formId" } } } } };
        OrderedJson rematched = Node("0x09000900", "Old.esp|0x000900", 1, 5, 5, {}, {});
        rematched["editorId"] = "Renamed";
        OrderedJson restoration = { { "nodes", { Node("0x05000801", "", 1, 9, 9, {}, {}), rematched } } };
        restoration["nodes"][0]["locks"] = { { { "nodeId", "0x05000801" } } };
        return { { "version", "1.0" }, { "schools", { { "Destruction", destruction }, { "Restoration", restoration } } },
            { "bridges", { { { "from", "0x05000801" }, { "to", "0x09000900" } } } } };
    }

    FakeGame RefreshGame()
    {
        FakeGame game;
        game.resolves = { { "Skyrim.esm|0x012FCD", "0x00012FCD" }, { "ModA.esp|0x000801", "0x04000801" },
            { "ModH.esp|0x000803", "0x06000802" } };
        game.rematch = { { "Old.esp|0x000900", { "0x09000900", "New.esp|0x000900" } } };
        return game;
    }

    int RefreshCase()
    {
        std::printf("stale formIds of live nodes refreshed\n");
        Checker check;
        OrderedJson tree = RefreshTree();
        const OrderedJson before = tree;
        const auto verdicts = Validate(tree, RefreshGame());
        check.Check(verdicts.size() == 6, std::format("one verdict per node with a formId ({})", verdicts.size()));
        const auto refresh = TreeGraftRun::RefreshIds(tree, verdicts);

        check.Check(NodeAt(tree, "Destruction", 1)["formId"] == "0x04000801", "a live node takes its current formId");
        check.Check(NodeAt(tree, "Destruction", 0)["children"] == OrderedJson({ "0x04000801", "0x06000802" }),
            "links to it follow (children)");
        check.Check(NodeAt(tree, "Destruction", 2)["formId"] == "0x06000802" &&
                        NodeAt(tree, "Destruction", 2)["persistentId"] == "Gone.esp|0x000802",
            "a removed plugin's node keeps its id");
        check.Check(NodeAt(tree, "Destruction", 2)["children"] == OrderedJson({ "0x04000801" }),
            "its links to a refreshed node follow too (back in its place when the mod returns)");
        check.Check(NodeAt(tree, "Destruction", 3)["formId"] == "0x07000803",
            "held: a node whose new formId is a kept node's saved one stays as it was");
        check.Check(NodeAt(tree, "Destruction", 3)["prerequisites"] == OrderedJson({ "0x04000801" }), "its links still follow");
        check.Check(refresh.held.size() == 1 && refresh.held[0] == "0x07000803", "held listed");
        check.Check(NodeAt(tree, "Restoration", 0)["formId"] == "0x04000801" &&
                        NodeAt(tree, "Restoration", 0)["locks"][0]["nodeId"] == "0x04000801",
            "a legacy copy saved under the same formId follows, and a lock on it");
        check.Check(NodeAt(tree, "Restoration", 1)["persistentId"] == "New.esp|0x000900" &&
                        NodeAt(tree, "Restoration", 1)["formId"] == "0x09000900",
            "a rematched node gets its new persistentId (same formId)");
        check.Check(tree["bridges"][0]["from"] == "0x04000801" && tree["bridges"][0]["to"] == "0x09000900", "bridges follow");
        check.Check(NodeAt(tree, "Destruction", 4) == before["schools"]["Destruction"]["nodes"][4], "a node without a formId as it was");
        check.Check(tree["schools"]["Destruction"]["root"] == "0x00012FCD", "an unmoved root stays");
        check.Check(refresh.nodesMoved == 2 && refresh.persistentIdsWritten == 1,
            std::format("counts: {} moved, {} persistentIds", refresh.nodesMoved, refresh.persistentIdsWritten));
        check.Check(tree.dump().find(TreeGraftRun::kNodeTag) == std::string::npos, "no tag in the file's tree");
        const auto rematched = TreeGraftRun::RematchedIds(before, verdicts);
        check.Check(rematched.size() == 1 && rematched.begin()->first == "old.esp|0x000900" &&
                        rematched.begin()->second == "New.esp|0x000900",
            "the rematched node, from the verdicts");
        check.Check(TreeGraftRun::PreGraftNodesKept(before, tree, rematched).empty(),
            "the verification takes an id refresh as no change: " + TreeGraftRun::PreGraftNodesKept(before, tree, rematched));
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, tree, {}).empty(),
            "a persistentId change no verdict rematched: not kept");

        // A chain: P moves onto Q's saved id, Q is held, so P is held too
        OrderedJson chain = { { "schools", { { "S", { { "nodes", {
            Node("0x01000001", "P.esp|0x000001", 1, 0, 0, {}, {}), Node("0x01000002", "Q.esp|0x000002", 1, 1, 1, {}, {}),
            Node("0x01000003", "Gone.esp|0x000003", 1, 2, 2, {}, {}) } } } } } } };
        FakeGame chainGame;
        chainGame.resolves = { { "P.esp|0x000001", "0x01000002" }, { "Q.esp|0x000002", "0x01000003" } };
        const auto chained = TreeGraftRun::RefreshIds(chain, Validate(chain, chainGame));
        check.Check(NodeAt(chain, "S", 0)["formId"] == "0x01000001" && NodeAt(chain, "S", 1)["formId"] == "0x01000002" &&
                        chained.held.size() == 2,
            "a chain onto a held id is held whole");

        // Verdicts that do not fit the tree: nothing written
        OrderedJson untouched = RefreshTree();
        bool threw = false;
        try {
            (void)TreeGraftRun::RefreshIds(untouched, std::vector<TreeGraftRun::NodeVerdict>(2));
        } catch (const std::exception&) {
            threw = true;
        }
        check.Check(threw && untouched == RefreshTree(), "verdicts of another tree: throws, the tree untouched");
        return check.failed;
    }

    int LiveScanCase()
    {
        std::printf("the scan the graft sees\n");
        Checker check;
        OrderedJson tree = { { "schools", { { "S", { { "nodes", {
            Node("0x01000001", "Keep.esp|0x000001", 1, 0, 0, {}, {}), Node("0x01000002", "Gated.esp|0x000002", 1, 1, 1, {}, {}),
            Node("0x01000003", "", 1, 2, 2, {}, {}) } } } } } } };
        FakeGame game;
        game.resolves = { { "Keep.esp|0x000001", "0x01000001" }, { "Gated.esp|0x000002", "0x01000002" } };
        game.notPlayerSpells = { "Gated.esp|0x000002" };
        const auto verdicts = Validate(tree, game);
        Json scan = Json::array({ Spell("0x01000001", "Keep.esp", "0x000001", "k", "S", "Novice"),
            Spell("0x01000002", "Gated.esp", "0x000002", "g", "S", "Novice"),
            Spell("0x01000003", "Legacy.esp", "0x000003", "l", "S", "Novice"),
            Spell("0x01000004", "New.esp", "0x000004", "n", "S", "Novice") });
        std::size_t left = 0;
        const auto live = TreeGraftRun::LiveScanForGraft(std::move(scan), tree, verdicts, left);
        std::set<std::string> names;
        for (const auto& spell : live) names.insert(spell["name"].get<std::string>());
        check.Check(names == std::set<std::string>{ "k", "n" } && left == 2,
            "spells only dropped nodes hold leave (no player's spell, a legacy node not found): never parents");
        return check.failed;
    }

    int DeclutterCase()
    {
        std::printf("declutter of the grafted nodes only\n");
        Checker check;
        OrderedJson tree = { { "globe", { { "x", 0 }, { "y", 0 }, { "radius", 45 } } }, { "layoutMode", "sun" },
            { "schools", { { "S", { { "startAngle", -10 }, { "endAngle", 60 }, { "nodes", {
                Node("0x01", "A|0x1", 1, 100, 0, { "0x02", "0x03" }, {}), Node("0x02", "A|0x2", 2, 160, 0, {}, { "0x01" }),
                Node("0x03", "N|0x3", 2, 160.5, 0.5, {}, { "0x01" }) } } } } } } };
        const std::map<std::string, std::size_t> preCounts = { { "S", 2 } };
        std::vector<TreeGraftRun::DeclutterSlot> slots;
        const Json request = TreeGraftRun::DeclutterRequest(tree, preCounts, slots);
        const auto& nodes = request["schools"][0]["nodes"];
        check.Check(request["spread"] == false && nodes.size() == 3 && nodes[0]["fixed"] == true && nodes[1]["fixed"] == true &&
                        nodes[2]["fixed"] == false,
            "existing nodes fixed, the grafted one free, no spread");
        const Json reply = LayoutDeclutter::Run(request);
        std::size_t moved = 0;
        const auto error = TreeGraftRun::ApplyDeclutter(tree, reply, slots, preCounts, moved);
        check.Check(error.empty(), "applied: " + error);
        check.Check(NodeAt(tree, "S", 0)["x"] == 100 && NodeAt(tree, "S", 1)["x"] == 160 && NodeAt(tree, "S", 1)["y"] == 0,
            "existing nodes exactly where they were");
        const double dx = NodeAt(tree, "S", 2)["x"].get<double>() - 160, dy = NodeAt(tree, "S", 2)["y"].get<double>();
        check.Check(moved == 1 && dx * dx + dy * dy > 100, "the grafted node moved off the one it sat on");

        Json wrong = reply;
        wrong["positions"][0][0] = "0x99";
        check.Check(!TreeGraftRun::ApplyDeclutter(tree, wrong, slots, preCounts, moved).empty(),
            "a reply in another order: an error, nothing taken");
        return check.failed;
    }

    int VerifyCase()
    {
        std::printf("verification\n");
        Checker check;
        const OrderedJson before = { { "schools", { { "S", { { "nodes", {
            Node("0x01", "A|0x1", 1, 100, 0, { "0x02" }, {}), Node("0x02", "A|0x2", 2, 160, 0, {}, { "0x01" }) } } } } } } };
        OrderedJson after = before;
        after["schools"]["S"]["nodes"][0]["children"].push_back("0x03");
        after["schools"]["S"]["nodes"].push_back(Node("0x03", "N|0x3", 2, 100, 60, {}, { "0x01" }));
        check.Check(TreeGraftRun::PreGraftNodesKept(before, after, {}).empty(), "a parent gaining the grafted child: kept");

        auto moved = after;
        moved["schools"]["S"]["nodes"][1]["x"] = 161;
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, moved, {}).empty(), "a node moved: not kept");
        auto retiered = after;
        retiered["schools"]["S"]["nodes"][1]["tier"] = 3;
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, retiered, {}).empty(), "a tier changed: not kept");
        auto foreign = after;
        foreign["schools"]["S"]["nodes"][1]["children"].push_back("0x01");
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, foreign, {}).empty(), "a child that is not a grafted node: not kept");
        auto relinked = after;
        relinked["schools"]["S"]["nodes"][1]["prerequisites"] = { "0x03" };
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, relinked, {}).empty(), "a prerequisite changed: not kept");
        auto respelled = after;
        respelled["schools"]["S"]["nodes"][1]["persistentId"] = "Other.esp|0x2";
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, respelled, { { "a|0x1", "Other.esp|0x2" } }).empty(),
            "a persistentId change the rematch map names for another node: not kept");
        check.Check(TreeGraftRun::PreGraftNodesKept(before, respelled, { { "a|0x2", "Other.esp|0x2" } }).empty(),
            "a persistentId change the rematch map names for this node: kept");
        auto gone = before;
        gone["schools"]["S"]["nodes"].erase(1);
        check.Check(!TreeGraftRun::PreGraftNodesKept(before, gone, {}).empty(), "a node gone: not kept");

        // Parents, on the validated tree the panel gets
        const Json validated = Json(after);
        check.Check(TreeGraftRun::GraftedHaveLiveParents(validated, after, { { "S", 2 } }).empty(), "grafted under a live parent");
        Json orphan = validated;
        orphan["schools"]["S"]["nodes"].erase(0);
        check.Check(!TreeGraftRun::GraftedHaveLiveParents(orphan, after, { { "S", 2 } }).empty(),
            "the parent not in the validated tree: fails");

        check.Check(TreeGraftRun::SeenGrafted({ "N|0x3" }, { "A|0x1" }).empty(), "no seen spell grafted");
        check.Check(!TreeGraftRun::SeenGrafted({ "n|0x3" }, { "N|0X3" }).empty(), "a seen spell grafted (any case): fails");
        return check.failed;
    }

    int EndToEndCase()
    {
        std::printf("the whole plan: a stale live node, a new spell on its old formId\n");
        Checker check;
        const OrderedJson tree = { { "version", "1.0" }, { "globe", { { "x", 0 }, { "y", 0 }, { "radius", 45 } } },
            { "schools", { { "Destruction", { { "root", "0x00012FCD" }, { "startAngle", -40 }, { "endAngle", 40 }, { "nodes", {
                Node("0x00012FCD", "Skyrim.esm|0x012FCD", 1, 100, 0, { "0x05000801", "0x06000802" }, {}),
                Node("0x05000801", "ModA.esp|0x000801", 2, 170, 20, {}, { "0x00012FCD" }),
                Node("0x06000802", "Gone.esp|0x000802", 2, 170, -20, {}, { "0x00012FCD" }) } } } } } } };
        const std::vector<std::string> traits = { "element.fire", "school.destruction" };
        const Json scan = Json::array({ Spell("0x00012FCD", "Skyrim.esm", "0x012FCD", "Flames", "Destruction", "Novice", traits),
            Spell("0x04000801", "ModA.esp", "0x000801", "Firebolt", "Destruction", "Apprentice", traits),
            Spell("0x05000801", "NewMod.esp", "0x000801", "Fire Lance", "Destruction", "Apprentice", traits),
            Spell("0x05000802", "NewMod.esp", "0x000802", "Fire Wall", "Destruction", "Adept", traits),
            Spell("0x05000803", "NewMod.esp", "0x000803", "Banned", "Destruction", "Novice", traits),
            Spell("0x05000804", "NewMod.esp", "0x000804", "Shield", "Alteration", "Novice") });
        FakeGame game;
        game.resolves = { { "Skyrim.esm|0x012FCD", "0x00012FCD" }, { "ModA.esp|0x000801", "0x04000801" } };

        TreeGraftRun::PlanInput input;
        input.tree = tree;
        input.scanSpells = scan;
        input.record.loadOrder = "old";
        input.baseline = { { "Skyrim.esm|0x012FCD", "ModA.esp|0x000801" }, TreeGraftRun::SeenSource::kRecord };
        input.rules.blacklistKeys = { "newmod.esp:000803" };
        input.verdicts = Validate(tree, game);
        input.loadOrder = "new";
        const auto plan = TreeGraftRun::PlanGraft(std::move(input), TreeBuilder::BuildConfig{});

        check.Check(plan.deferred.empty(), std::format("nothing deferred ({})", plan.deferred.size()));
        check.Check(std::ranges::find(plan.grafted, "NewMod.esp|0x000801") != plan.grafted.end() &&
                        std::ranges::find(plan.grafted, "NewMod.esp|0x000802") != plan.grafted.end() && plan.grafted.size() == 2,
            "the spell on the stale node's old formId is grafted, and its sibling");
        check.Check(plan.leftOut == std::vector<std::string>{ "NewMod.esp|0x000803" }, "the blacklisted one left out");
        check.Check(plan.skippedNoSchool == std::vector<std::string>{ "NewMod.esp|0x000804" }, "the Alteration one unplaced");
        check.Check(plan.writeTree && plan.writeRecord, "tree and record written");
        check.Check(NodeAt(plan.tree, "Destruction", 1)["formId"] == "0x04000801", "the stale node refreshed in the file");
        check.Check(NodeAt(plan.tree, "Destruction", 2)["formId"] == "0x06000802" &&
                        NodeAt(plan.tree, "Destruction", 2)["x"] == 170 && NodeAt(plan.tree, "Destruction", 2)["y"] == -20,
            "the removed plugin's node kept as it was");
        check.Check(plan.problem.empty(), "the verification passes: " + plan.problem);
        check.Check(plan.record.seen.size() == 5 && plan.record.loadOrder == "old" && plan.record.pendingGrafted == 2,
            std::format("record: seen + 2 grafted + 1 left out ({}), load order kept for the unplaced one, 2 to tell",
                plan.record.seen.size()));
        check.Check(TreeGraftRun::GraftedHaveLiveParents(Json(plan.tree), plan.tree, { { "Destruction", 3 } }).empty(),
            "each grafted node under a node of its school");

        // The same without the refresh: the new spell is deferred (TreeGraft.h's precondition)
        std::vector<TreeGraftRun::NodeVerdict> keepAsSaved;
        for (const auto& node : tree["schools"]["Destruction"]["nodes"]) {
            keepAsSaved.push_back({ true, node["formId"].get<std::string>(), node["persistentId"].get<std::string>() });
        }
        TreeGraftRun::PlanInput unrefreshed;
        unrefreshed.tree = tree;
        unrefreshed.scanSpells = scan;
        unrefreshed.baseline = { { "Skyrim.esm|0x012FCD", "ModA.esp|0x000801" }, TreeGraftRun::SeenSource::kRecord };
        unrefreshed.verdicts = keepAsSaved;
        unrefreshed.loadOrder = "new";
        const auto stale = TreeGraftRun::PlanGraft(std::move(unrefreshed), TreeBuilder::BuildConfig{});
        check.Check(std::ranges::find(stale.deferred, "NewMod.esp|0x000801") != stale.deferred.end(),
            "without the refresh that spell would be deferred");

        // Nothing new: no tree write; the record takes the load order
        TreeGraftRun::PlanInput none;
        none.tree = tree;
        none.scanSpells = Json::array({ scan[0], scan[1] });
        none.baseline = { { "Skyrim.esm|0x012FCD", "ModA.esp|0x000801" }, TreeGraftRun::SeenSource::kRecord };
        none.record.loadOrder = "old";
        none.record.hasSeen = true;
        none.record.seen = none.baseline.seen;
        none.verdicts = Validate(tree, game);
        none.loadOrder = "new";
        const auto quiet = TreeGraftRun::PlanGraft(std::move(none), TreeBuilder::BuildConfig{});
        check.Check(!quiet.writeTree && quiet.writeRecord && quiet.record.loadOrder == "new",
            "no new spell: the tree is not written, the record is this load order's");

        // ModA.esp renamed ModB.esp: the node found again by editor id; its new persistentId is seen
        TreeGraftRun::PlanInput renamed;
        renamed.tree = tree;
        renamed.scanSpells = Json::array({ scan[0], Spell("0x04000801", "ModB.esp", "0x000801", "Firebolt", "Destruction",
                                                         "Apprentice", traits) });
        renamed.baseline = { { "Skyrim.esm|0x012FCD", "ModA.esp|0x000801" }, TreeGraftRun::SeenSource::kRecord };
        renamed.verdicts = Validate(tree, game);
        renamed.verdicts[1].persistentId = "ModB.esp|0x000801";
        renamed.loadOrder = "new";
        const auto rematch = TreeGraftRun::PlanGraft(std::move(renamed), TreeBuilder::BuildConfig{});
        check.Check(rematch.problem.empty() && rematch.grafted.empty() && rematch.writeRecord &&
                        std::ranges::find(rematch.record.seen, "ModB.esp|0x000801") != rematch.record.seen.end() &&
                        rematch.record.seen.size() == 3 && rematch.record.pendingGrafted == 0,
            std::format("a rematched node's new persistentId joins the seen list, nothing grafted or told ({} seen)",
                rematch.record.seen.size()));
        return check.failed;
    }


    int SelfTest()
    {
        const std::vector<int (*)()> cases = { TriggerCase, RulesCase, BaselineCase, RecordCase, RefreshCase, LiveScanCase,
            DeclutterCase, VerifyCase, EndToEndCase, GraftRunRecordCases::DumpCase, GraftRunRecordCases::SeedCase,
            GraftRunRecordCases::JournalCase, GraftRunRecordCases::LostCase };
        int failedCases = 0;
        for (auto* run : cases) {
            try {
                if (run() > 0) ++failedCases;
            } catch (const std::exception& e) {
                std::printf("    [FAIL] threw: %s\n", e.what());
                ++failedCases;
            }
        }
        std::printf("self-test: %d of %zu cases failed\n", failedCases, cases.size());
        return failedCases == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: graft-run-check --self-test\n");
        return kExitUsage;
    }
    spdlog::set_level(spdlog::level::warn);
    return SelfTest();
}
