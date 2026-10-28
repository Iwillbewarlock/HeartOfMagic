// graft-check - offline check of the tree graft (plugins/spelllearning/include/treebuilder/
// GraftCandidates.h and TreeGraft.h): new spells join a saved tree without moving it.
//
//   graft-check --self-test     made-up trees and scans, no game needed
//
// What is new (NewSpells): never seen, not in the tree by formId or persistentId, has a
// persistentId, no node of its duplicate group. What the tree takes (IsTreeCandidate): the
// panel's rule, case by case (proceduralTreeBuilder.js isTaughtByTome, the blacklist and the
// plugin whitelist). Where it goes (Graft): every existing node as it was, a parent gaining a
// child, the same tree for the same input, a fire spell under the fire branch, a school the
// tree lacks left alone, a formId the file already holds (or no persistentId) deferred,
// 300 spells at once.
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "Common.h"

#include "treebuilder/GraftCandidates.h"
#include "treebuilder/TreeGraft.h"

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

namespace
{
    using Json = nlohmann::json;
    using TreeGraft::PlayerRules;

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
    // Made-up scans and trees
    // ------------------------------------------------------------------------

    /// A scan spell the tree would take, from a plugin, with its traits
    Json Spell(const std::string& formId, const std::string& plugin, const std::string& local, const std::string& name,
        const std::string& school, const std::string& level, const std::vector<std::string>& traits = {})
    {
        Json spell = { { "formId", formId }, { "persistentId", plugin + "|" + local }, { "plugin", plugin },
            { "name", name }, { "editorId", name }, { "school", school }, { "skillLevel", level },
            { "taughtByTome", true }, { "traits", traits } };
        return spell;
    }

    /// A saved node as the panel writes it (classicTreeOutput.js)
    Json Node(const std::string& formId, const std::string& persistentId, int tier, double x, double y,
        const std::vector<std::string>& children, const std::vector<std::string>& prereqs)
    {
        Json node = { { "formId", formId }, { "children", children }, { "prerequisites", prereqs },
            { "hardPrereqs", Json::array() }, { "softPrereqs", prereqs },
            { "softNeeded", prereqs.empty() ? 0 : 1 }, { "tier", tier }, { "x", x }, { "y", y } };
        if (!persistentId.empty()) node["persistentId"] = persistentId;
        return node;
    }

    std::vector<std::string> Names(const std::vector<const Json*>& spells)
    {
        std::vector<std::string> out;
        for (const auto* s : spells) out.push_back(s->value("name", std::string()));
        return out;
    }

    std::string Join(const std::vector<std::string>& items)
    {
        std::string out;
        for (const auto& item : items) out += (out.empty() ? "" : ", ") + item;
        return out;
    }

    /// Every node of the tree by "school/formId"
    std::map<std::string, Json> NodesOf(const Json& tree)
    {
        std::map<std::string, Json> out;
        for (const auto& [school, data] : tree["schools"].items()) {
            for (const auto& node : data["nodes"]) out[school + "/" + node["formId"].get<std::string>()] = node;
        }
        return out;
    }

    /// Every node of `before` is in `after` with every field as it was, except that a parent's
    /// children may have gained grafted ids at the end. Returns how many nodes gained a child.
    bool ExistingKept(const Json& before, const Json& after, const std::set<std::string>& grafted, int& parentsGained)
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
    const Json* GraftedNode(const Json& tree, const std::string& school, const std::string& formId)
    {
        for (const auto& node : tree["schools"][school]["nodes"]) {
            if (node["formId"] == formId) return &node;
        }
        return nullptr;
    }

    std::vector<const Json*> Pointers(const std::vector<Json>& spells)
    {
        std::vector<const Json*> out;
        for (const auto& s : spells) out.push_back(&s);
        return out;
    }

    // A small Destruction tree: Flames (root, fire), Firebolt (fire) and Ice Spike (frost) under it.
    // Positions on a spoke along +x from the globe at 0,0, branches above and below.
    Json SmallTree()
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

    std::vector<Json> SmallScan()
    {
        return { Spell("0x00012FCD", "Skyrim.esm", "0x012FCD", "Flames", "Destruction", "Novice", { "element.fire", "kind.damage" }),
            Spell("0x0001C789", "Skyrim.esm", "0x01C789", "Firebolt", "Destruction", "Apprentice", { "element.fire", "kind.damage" }),
            Spell("0x0002B96C", "Skyrim.esm", "0x02B96C", "IceSpike", "Destruction", "Apprentice", { "element.frost", "kind.damage" }) };
    }

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
        std::printf("  rules (IsTreeCandidate)\n");
        Checker check;
        const Json base = Spell("0x05001234", "MyMod.ESP", "0x001234", "Base", "Destruction", "Novice");
        PlayerRules none;
        check.Check(TreeGraft::IsTreeCandidate(base, none), "a tome spell with no marks is taken");

        auto with = [&base](const char* key, const Json& value) {
            Json s = base;
            s[key] = value;
            return s;
        };
        const Json gateOut = with("gate", { { "band", "out" }, { "reasons", { "free" } } });
        const Json gateReview = with("gate", { { "band", "review" } });
        const Json questOnly = with("questOnly", true);
        const Json unreachable = with("tomeUnreachable", true);
        check.Check(!TreeGraft::IsTreeCandidate(gateOut, none), "rules: gate out is left out");
        check.Check(TreeGraft::IsTreeCandidate(gateReview, none), "gate review stays in");
        check.Check(!TreeGraft::IsTreeCandidate(questOnly, none), "rules: questOnly is left out");
        check.Check(!TreeGraft::IsTreeCandidate(unreachable, none), "rules: tomeUnreachable is left out");
        check.Check(!TreeGraft::IsTreeCandidate(with("taughtByTome", false), none), "no tome teaches it: left out");
        check.Check(!TreeGraft::IsTreeCandidate(with("voiceSlot", true), none), "voice slot: left out");
        check.Check(!TreeGraft::IsTreeCandidate(with("duplicateOf", "0x01000001"), none), "another plugin's copy: left out");
        check.Check(TreeGraft::IsTreeCandidate(with("duplicateOf", ""), none), "an empty duplicateOf is no copy");
        check.Check(!TreeGraft::IsTreeCandidate(with("freeSpell", true), none), "a scan without gate: freeSpell left out");
        Json reviewedFree = gateReview;
        reviewedFree["freeSpell"] = true;
        check.Check(TreeGraft::IsTreeCandidate(reviewedFree, none), "with a gate, freeSpell does not count");

        PlayerRules kept;
        kept.gateKept = { "mymod.esp:001234" };   // GateReview.key: spell.plugin folded A-Z, local id
        check.Check(TreeGraft::IsTreeCandidate(gateOut, kept), "gateKept brings gate-out back");
        check.Check(TreeGraft::IsTreeCandidate(questOnly, kept), "gateKept brings questOnly back");
        check.Check(!TreeGraft::IsTreeCandidate(unreachable, kept), "gateKept does not bring tomeUnreachable back");

        PlayerRules blacklist;
        blacklist.blacklistKeys = { "mymod.esp:001234" };
        check.Check(!TreeGraft::IsTreeCandidate(base, blacklist), "rules: blacklist by plugin and local id leaves it out");
        PlayerRules blacklistById;
        blacklistById.blacklistKeys = { "0x05001234" };
        check.Check(!TreeGraft::IsTreeCandidate(base, blacklistById), "rules: blacklist by formId only leaves it out");
        Json otherPlugin = base;
        otherPlugin["persistentId"] = "Real.esp|0x001234";   // the blacklist reads the persistentId's plugin first
        PlayerRules blacklistReal;
        blacklistReal.blacklistKeys = { "real.esp:001234" };
        check.Check(!TreeGraft::IsTreeCandidate(otherPlugin, blacklistReal), "the blacklist goes by the persistentId's plugin");

        PlayerRules off;
        off.pluginsOff = { "mymod.esp" };
        check.Check(!TreeGraft::IsTreeCandidate(base, off), "rules: plugin off in the whitelist leaves it out");
        // An entry an older panel lowered with toLowerCase still turns its plugin off (proceduralTreeBuilder.js:154)
        const Json cyrillic = Spell("0x07000011", "\xD0\x97\xD0\xB0\xD0\xBA.esp", "0x000011", "Cyr", "Destruction", "Novice");
        PlayerRules loweredOff;
        loweredOff.pluginsOff = { "\xD0\xB7\xD0\xB0\xD0\xBA.esp" };   // Cyrillic "Zak" lowered by toLowerCase
        check.Check(!TreeGraft::IsTreeCandidate(cyrillic, loweredOff), "a whitelist entry lowered with toLowerCase matches");
        check.Check(TreeGraft::Detail::JsLowerCase("\xC3\x89\xC4\xB0\xCE\xA3" "X") == "\xC3\xA9" "i" "\xCC\x87\xCF\x83" "x",
            "toLowerCase: E acute, dotted capital I (i + U+0307), capital sigma, X");
        PlayerRules offOther;
        offOther.pluginsOff = { "other.esp" };
        check.Check(TreeGraft::IsTreeCandidate(base, offOther), "another plugin off changes nothing");

        // ESL: the local id is the last 3 hex digits (getLocalFormId)
        const Json esl = Spell("0xFE0A1801", "Light.esl", "0x801", "Light", "Destruction", "Novice");
        PlayerRules eslBlack;
        eslBlack.blacklistKeys = { "light.esl:801" };
        check.Check(!TreeGraft::IsTreeCandidate(esl, eslBlack), "an ESL spell's key has the 3-digit local id");
        // Non-ASCII bytes are kept as they are (foldPluginName folds A-Z only)
        const Json korean = Spell("0x07000010", "\xED\x95\x9C.esp", "0x000010", "Korean", "Destruction", "Novice");
        PlayerRules koreanOff;
        koreanOff.pluginsOff = { "\xED\x95\x9C.esp" };
        check.Check(!TreeGraft::IsTreeCandidate(korean, koreanOff), "a non-ASCII plugin name matches byte for byte");
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
        check.Check(maxChildren <= TreeBuilder::BuildConfig{}.maxChildrenPerNode,
            std::format("no node over the child cap ({} at most)", maxChildren));
        return check.failed;
    }

    int SelfTest()
    {
        const std::vector<int (*)()> cases = { CandidatesCase, RulesCase, GraftKeepsCase, RepeatableCase, FireBranchCase,
            NoSchoolCase, DeferredCase, OrderedCase, ManyCase };
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
