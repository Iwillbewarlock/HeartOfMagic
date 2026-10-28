// graft-check - offline check of the tree graft (plugins/spelllearning/include/treebuilder/
// GraftCandidates.h and TreeGraft.h): new spells join a saved tree without moving it.
//
//   graft-check --self-test     made-up trees and scans, no game needed
//
// What is new (NewSpells): never seen, not in the tree by formId or persistentId, has a
// persistentId, no node of its duplicate group. What the tree takes (IsBuildCandidate): the
// panel's rule, case by case (proceduralTreeBuilder.js isTaughtByTome, the blacklist and the
// plugin whitelist). Where it goes (Graft): every existing node as it was, a parent gaining a
// child, the same tree for the same input, a fire spell under the fire branch, a school the
// tree lacks left alone, a formId the file already holds (or no persistentId) deferred,
// 300 spells at once.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "graft-check.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

using namespace GraftCheck;

namespace
{
    // ------------------------------------------------------------------------
    // Cases
    // ------------------------------------------------------------------------

    int CandidatesCase()
    {
        std::printf("  candidates (NewSpells)\n");
        Checker check;
        const Json tree = { { "schools",
            { { "Destruction",
                { { "root", "0x00000A01" },
                    { "nodes",
                        { Node("0x00000A01", "", 1, 0, 0, { "0x02000801_dup_1" }, {}),                    // old tree: no persistentId
                            Node("0x02000801_dup_1", "", 1, 10, 0, {}, { "0x00000A01" }),                 // edit mode copy
                            Node("0x05000900", "Moved.esp|0x000900", 2, 20, 0, {}, {}),                  // formId stale
                            Node("0x0A000800", "Old.esp|0x000800", 2, 30, 0, {}, {}),                    // now a copy of New's
                            Node("0x0B000700", "Kept.esp|0x000700", 2, 40, 0, {}, {}),                   // a group's kept spell
                            Node("0x0E000600", "Gone.esp|0x000600", 2, 50, 0, {}, {}) } } } } } } };     // a removed plugin's
        Json scan = Json::array({
            Spell("0x01000001", "New.esp", "0x000001", "Fresh", "Destruction", "Novice"),
            Spell("0x01000002", "New.esp", "0x000002", "Seen", "Destruction", "Novice"),
            Spell("0x00000A01", "Skyrim.esm", "0x000A01", "ByFormIdOnly", "Destruction", "Novice"),
            Spell("0x02000801", "Spawn.esp", "0x000801", "SpawnedCopy", "Destruction", "Novice"),
            Spell("0x06000900", "Moved.esp", "0x000900", "ByPersistentId", "Destruction", "Novice"),
            Spell("0x0A000800", "Old.esp", "0x000800", "OldRecord", "Destruction", "Novice"),
            Spell("0x0C000800", "New.esp", "0x000800", "NewKeptCopy", "Destruction", "Novice"),
            Spell("0x0D000700", "Copy.esp", "0x000700", "CopyOfKept", "Destruction", "Novice"),
            Spell("0x01000003", "New.esp", "0x000003", "NoPersistentId", "Destruction", "Novice"),
            Spell("0x0B000700", "Kept.esp", "0x000700", "KeptRecord", "Destruction", "Novice"),
            Spell("0x0E000600", "Took.esp", "0x000600", "TookGoneFormId", "Destruction", "Novice"),
        });
        scan[5]["duplicateOf"] = "0x0C000800";   // the old record is now the copy, the new mod's the kept one
        scan[7]["duplicateOf"] = "0x0B000700";   // a copy of a spell the tree has
        scan[8].erase("persistentId");
        const std::unordered_set<std::string> seen = { "New.esp|0x000002" };

        const auto names = Names(TreeGraft::NewSpells(scan, tree, seen));
        const auto has = [&names](const char* n) { return std::ranges::find(names, n) != names.end(); };
        check.Check(has("Fresh"), "a spell nothing has seen is new");
        check.Check(!has("Seen"), "candidates: a seen spell is not new");
        check.Check(!has("ByFormIdOnly"), "in the tree by formId only (no persistentId on the node) is not new");
        check.Check(!has("SpawnedCopy"), "an edit mode copy's node (formId_dup_n) has its spell in the tree");
        check.Check(!has("ByPersistentId"), "in the tree by persistentId (the node's formId is stale) is not new");
        check.Check(!has("NewKeptCopy"), "a new copy whose group has a node is not new");
        check.Check(!has("CopyOfKept"), "a copy of a spell the tree has is not new");
        check.Check(!has("NoPersistentId"), "no persistentId is not new");
        check.Check(has("TookGoneFormId"),
            "a removed plugin's node (persistentId gone) does not block the new spell that has its old formId");
        check.Check(names.size() == 2, "only Fresh and TookGoneFormId are new: " + Join(names));

        const std::unordered_set<std::string> seenOtherCase = { "NEW.ESP|0x000001" };
        check.Check(TreeGraft::NewSpells(Json::array({ scan[0] }), tree, seenOtherCase).empty(),
            "the seen list is matched with A-Z folded");
        check.Check(TreeGraft::NewSpells(Json::object(), tree, {}).empty(), "a scan that is no array gives nothing");
        return check.failed;
    }

    int RulesCase()
    {
        std::printf("  rules (IsBuildCandidate)\n");
        Checker check;
        const Json base = Spell("0x05001234", "MyMod.ESP", "0x001234", "Base", "Destruction", "Novice");
        PlayerRules none;
        check.Check(TreeGraft::IsBuildCandidate(base, none), "a tome spell with no marks is taken");

        auto with = [&base](const char* key, const Json& value) {
            Json s = base;
            s[key] = value;
            return s;
        };
        const Json gateOut = with("gate", { { "band", "out" }, { "reasons", { "free" } } });
        const Json gateReview = with("gate", { { "band", "review" } });
        const Json questOnly = with("questOnly", true);
        const Json unreachable = with("tomeUnreachable", true);
        check.Check(!TreeGraft::IsBuildCandidate(gateOut, none), "rules: gate out is left out");
        check.Check(TreeGraft::IsBuildCandidate(gateReview, none), "gate review stays in");
        check.Check(!TreeGraft::IsBuildCandidate(questOnly, none), "rules: questOnly is left out");
        check.Check(!TreeGraft::IsBuildCandidate(unreachable, none), "rules: tomeUnreachable is left out");
        check.Check(!TreeGraft::IsBuildCandidate(with("taughtByTome", false), none), "no tome teaches it: left out");
        check.Check(!TreeGraft::IsBuildCandidate(with("voiceSlot", true), none), "voice slot: left out");
        check.Check(!TreeGraft::IsBuildCandidate(with("duplicateOf", "0x01000001"), none), "another plugin's copy: left out");
        check.Check(TreeGraft::IsBuildCandidate(with("duplicateOf", ""), none), "an empty duplicateOf is no copy");
        check.Check(!TreeGraft::IsBuildCandidate(with("freeSpell", true), none), "a scan without gate: freeSpell left out");
        Json reviewedFree = gateReview;
        reviewedFree["freeSpell"] = true;
        check.Check(TreeGraft::IsBuildCandidate(reviewedFree, none), "with a gate, freeSpell does not count");

        PlayerRules kept;
        kept.gateKept = { "mymod.esp:001234" };   // GateReview.key: spell.plugin folded A-Z, local id
        check.Check(TreeGraft::IsBuildCandidate(gateOut, kept), "gateKept brings gate-out back");
        check.Check(TreeGraft::IsBuildCandidate(questOnly, kept), "gateKept brings questOnly back");
        check.Check(!TreeGraft::IsBuildCandidate(unreachable, kept), "gateKept does not bring tomeUnreachable back");

        PlayerRules blacklist;
        blacklist.blacklistKeys = { "mymod.esp:001234" };
        check.Check(!TreeGraft::IsBuildCandidate(base, blacklist), "rules: blacklist by plugin and local id leaves it out");
        PlayerRules blacklistById;
        blacklistById.blacklistKeys = { "0x05001234" };
        check.Check(!TreeGraft::IsBuildCandidate(base, blacklistById), "rules: blacklist by formId only leaves it out");
        Json otherPlugin = base;
        otherPlugin["persistentId"] = "Real.esp|0x001234";   // the blacklist reads the persistentId's plugin first
        PlayerRules blacklistReal;
        blacklistReal.blacklistKeys = { "real.esp:001234" };
        check.Check(!TreeGraft::IsBuildCandidate(otherPlugin, blacklistReal), "the blacklist goes by the persistentId's plugin");

        PlayerRules off;
        off.pluginsOff = { "mymod.esp" };
        check.Check(!TreeGraft::IsBuildCandidate(base, off), "rules: plugin off in the whitelist leaves it out");
        // An entry an older panel lowered with toLowerCase still turns its plugin off (proceduralTreeBuilder.js:154)
        const Json cyrillic = Spell("0x07000011", "\xD0\x97\xD0\xB0\xD0\xBA.esp", "0x000011", "Cyr", "Destruction", "Novice");
        PlayerRules loweredOff;
        loweredOff.pluginsOff = { "\xD0\xB7\xD0\xB0\xD0\xBA.esp" };   // Cyrillic "Zak" lowered by toLowerCase
        check.Check(!TreeGraft::IsBuildCandidate(cyrillic, loweredOff), "a whitelist entry lowered with toLowerCase matches");
        // Spell Research's filter asks the same rule by plugin name (LibrarianSpellResearchFilter.cpp PlayerLeftOut)
        check.Check(TreeGraft::PluginIsOff("\xD0\x97\xD0\xB0\xD0\xBA.esp", loweredOff.pluginsOff) &&
                        TreeGraft::PluginIsOff("MyMod.ESP", { "mymod.esp" }) && !TreeGraft::PluginIsOff("", { "" }),
            "PluginIsOff: the Cyrillic capital plugin with a toLowerCase key is off, A-Z folded too; no name is not off");
        check.Check(TreeGraft::Detail::JsLowerCase("\xC3\x89\xC4\xB0\xCE\xA3" "X") == "\xC3\xA9" "i" "\xCC\x87\xCF\x83" "x",
            "toLowerCase: E acute, dotted capital I (i + U+0307), capital sigma, X");
        check.Check(TreeGraft::Detail::JsLowerCase("\xE1\xBA\x9E" "\xD1\xA0") == "\xE1\xBA\x9E" "\xD1\xA0",
            "the documented limit: U+1E9E and U+0460 stay (JavaScript lowers them to U+00DF, U+0461)");
        PlayerRules offOther;
        offOther.pluginsOff = { "other.esp" };
        check.Check(TreeGraft::IsBuildCandidate(base, offOther), "another plugin off changes nothing");

        // ESL: the local id is the last 3 hex digits (getLocalFormId)
        const Json esl = Spell("0xFE0A1801", "Light.esl", "0x801", "Light", "Destruction", "Novice");
        PlayerRules eslBlack;
        eslBlack.blacklistKeys = { "light.esl:801" };
        check.Check(!TreeGraft::IsBuildCandidate(esl, eslBlack), "an ESL spell's key has the 3-digit local id");
        // Non-ASCII bytes are kept as they are (foldPluginName folds A-Z only)
        const Json korean = Spell("0x07000010", "\xED\x95\x9C.esp", "0x000010", "Korean", "Destruction", "Novice");
        PlayerRules koreanOff;
        koreanOff.pluginsOff = { "\xED\x95\x9C.esp" };
        check.Check(!TreeGraft::IsBuildCandidate(korean, koreanOff), "a non-ASCII plugin name matches byte for byte");
        return check.failed;
    }

    int GraftKeepsCase()
    {
        std::printf("  graft keeps the tree\n");
        Checker check;
        const Json tree = SmallTree();
        const auto scan = SmallScan();
        const std::vector<Json> news = { Spell("0x05000801", "Pyro.esp", "0x000801", "Fireball", "Destruction", "Adept",
            { "element.fire", "kind.damage" }) };
        const TreeBuilder::BuildConfig config;
        const auto result = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, config);
        int parentsGained = 0;
        check.Check(ExistingKept(tree, result.tree, { "0x05000801" }, parentsGained),
            "graft: every existing node's x, y, tier, prerequisites unchanged");
        check.Check(parentsGained == 1, std::format("one parent gained one child ({})", parentsGained));
        check.Check(result.grafted == std::vector<std::string>{ "Pyro.esp|0x000801" } && result.leftOut.empty() &&
                        result.skippedNoSchool.empty(), "the spell is listed as grafted by its persistentId");
        const Json* node = GraftedNode(result.tree, "Destruction", "0x05000801");
        check.Check(node != nullptr, "the new node is in its school");
        if (node) {
            const Json parent = (*node)["prerequisites"];
            check.Check(parent.size() == 1 && (*node)["softPrereqs"] == parent && (*node)["softNeeded"] == 1,
                "prerequisites and softPrereqs are [parent], softNeeded 1: " + node->dump());
            check.Check((*node)["persistentId"] == "Pyro.esp|0x000801" && (*node)["name"] == "Fireball" &&
                            (*node)["tier"] == 3 && (*node)["school"] == "Destruction" && (*node)["children"].empty() &&
                            (*node)["editorId"] == "Fireball" && (*node)["traits"] == Json::array({ "element.fire", "kind.damage" }),
                "persistentId, name, tier 3 (Adept), school, no children; the scan's editorId and traits baked");
            check.Check((*node)["x"].is_number() && (*node)["y"].is_number(), "it has a provisional x, y");
            bool apart = true;
            for (const auto& other : tree["schools"]["Destruction"]["nodes"]) {
                apart = apart && !((*node)["x"] == other["x"] && (*node)["y"] == other["y"]);
            }
            check.Check(apart, "not on top of an existing node");
        }
        return check.failed;
    }

    int RepeatableCase()
    {
        std::printf("  graft is repeatable\n");
        Checker check;
        const Json tree = SmallTree();
        const auto scan = SmallScan();
        std::vector<Json> news;
        for (int i = 0; i < 12; ++i) {
            const auto local = std::format("0x{:06X}", 0x900 + i);
            news.push_back(Spell(std::format("0x050{:05X}", 0x900 + i), "Pyro.esp", local, std::format("Spell{}", i),
                "Destruction", (i % 2) ? "Adept" : "Apprentice", { (i % 3) ? "element.fire" : "element.frost" }));
        }
        TreeBuilder::BuildConfig config;
        config.seed = 0;   // the build would seed from the clock: the graft has no randomness at all
        const auto a = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, config);
        const auto b = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, config);
        check.Check(a.tree.dump() == b.tree.dump() && a.grafted == b.grafted,
            "graft: repeatable - the same input twice gives the same tree");
        check.Check(a.grafted.size() == news.size(), std::format("all {} grafted", a.grafted.size()));
        std::set<std::string> newIds;
        for (const auto& spell : news) newIds.insert(spell["formId"].get<std::string>());
        check.Check(TierInversions(a.tree, "Destruction", newIds) == 0, "graft: no grafted node under a parent of a higher tier");
        std::set<std::pair<double, double>> points;
        for (const auto& node : a.tree["schools"]["Destruction"]["nodes"]) {
            if (!points.insert({ node["x"].get<double>(), node["y"].get<double>() }).second) {
                std::printf("      %s on a taken point %s, %s\n", node["formId"].get<std::string>().c_str(),
                    node["x"].dump().c_str(), node["y"].dump().c_str());
            }
        }
        check.Check(points.size() == a.tree["schools"]["Destruction"]["nodes"].size(), "no two nodes on one point");
        return check.failed;
    }

    int FireBranchCase()
    {
        std::printf("  graft follows the element\n");
        Checker check;
        Json tree = SmallTree();
        // A fire node of a plugin that is gone: the best fit, but no parent (not live)
        auto& nodes = tree["schools"]["Destruction"]["nodes"];
        nodes.push_back(Node("0x09000123", "Gone.esp|0x000123", 2, 250, 0, {}, { "0x00012FCD" }));
        nodes[0]["children"].push_back("0x09000123");
        const auto scan = SmallScan();
        std::vector<Json> news = {
            Spell("0x05000801", "Pyro.esp", "0x000801", "Fireball", "Destruction", "Adept", { "element.fire", "kind.damage" }),
            Spell("0x05000802", "Pyro.esp", "0x000802", "IceStorm", "Destruction", "Adept", { "element.frost", "kind.damage" }),
        };
        news[1]["editorId"] = "";   // no powerofthree's Tweaks
        const auto result = TreeGraft::Graft(tree, scan, Pointers(news), PlayerRules{}, TreeBuilder::BuildConfig{});
        const Json* fire = GraftedNode(result.tree, "Destruction", "0x05000801");
        const Json* frost = GraftedNode(result.tree, "Destruction", "0x05000802");
        check.Check(fire && (*fire)["prerequisites"] == Json::array({ "0x0001C789" }),
            "graft: a fire spell goes under the fire branch (element trait) not the frost one");
        check.Check(frost && (*frost)["prerequisites"] == Json::array({ "0x0002B96C" }) && !frost->contains("editorId"),
            "a frost spell under the frost one; its empty editor id is not baked");
        bool goneUntouched = false;
        for (const auto& node : result.tree["schools"]["Destruction"]["nodes"]) {
            if (node["formId"] == "0x09000123") goneUntouched = node == nodes.back();
        }
        check.Check(goneUntouched, "a missing plugin's node is never a parent and stays as it was");
        return check.failed;
    }

    int NoSchoolCase()
    {
        std::printf("  graft and a school the tree lacks\n");
        Checker check;
        const Json tree = SmallTree();
        const auto scan = SmallScan();
        const std::vector<Json> news = {
            Spell("0x05000a01", "Alter.esp", "0x000A01", "Stoneskin", "Alteration", "Novice", { "kind.armor" }),
            Spell("0x05000a02", "Alter.esp", "0x000A02", "Blessed", "Restoration", "Novice"),
        };
        Json leftOut = news[1];
        leftOut["questOnly"] = true;
        const std::vector<Json> both = { news[0], leftOut };
        const auto result = TreeGraft::Graft(tree, scan, Pointers(both), PlayerRules{}, TreeBuilder::BuildConfig{});
        check.Check(result.skippedNoSchool == std::vector<std::string>{ "Alter.esp|0x000A01" } && result.grafted.empty(),
            "graft: a school the tree lacks - spell in skippedNoSchool");
        check.Check(result.tree == tree, "tree unchanged");
        check.Check(result.leftOut == std::vector<std::string>{ "Alter.esp|0x000A02" }, "a spell the rules leave out is in leftOut");

        // A school whose nodes are all of missing plugins has no live parent either
        Json goneSchool = tree;
        goneSchool["schools"]["Alteration"] = { { "root", "0x09000001" },
            { "nodes", { Node("0x09000001", "Gone.esp|0x000001", 1, -100, 0, {}, {}) } } };
        const auto gone = TreeGraft::Graft(goneSchool, scan, Pointers({ news[0] }), PlayerRules{}, TreeBuilder::BuildConfig{});
        check.Check(gone.skippedNoSchool.size() == 1 && gone.tree == goneSchool, "a school with no live node: skipped, unchanged");
        return check.failed;
    }

    int SelfTest()
    {
        const std::vector<int (*)()> cases = { CandidatesCase, RulesCase, GraftKeepsCase, RepeatableCase, FireBranchCase,
            NoSchoolCase, GraftCheckCases::DeferredCase, GraftCheckCases::OrderedCase, GraftCheckCases::ManyCase,
            GraftCheckCases::FallbackCase, GraftCheckCases::RoomCase, GraftCheckCases::TreeConfigCase,
            GraftCheckCases::ChildPointCase };
        int failedCases = 0;
        for (auto* run : cases) {
            if (run() > 0) ++failedCases;
        }
        std::printf("self-test: %d of %zu cases failed\n", failedCases, cases.size());
        return failedCases == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: graft-check --self-test\n");
        return kExitUsage;
    }
    spdlog::set_level(spdlog::level::warn);
    return SelfTest();
}
