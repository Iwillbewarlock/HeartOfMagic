// tree-remap-check - offline check of a saved tree's load order fix
// (plugins/spelllearning/include/TreeRemap.h, run by SpellScanner::ValidateAndFixTree).
//
//   tree-remap-check --self-test     made-up trees and load orders, no game needed
//
// Each case is a saved tree (every link holds the formIds it was saved with) and
// what the game says now: the formId each persistentId resolves to, which plugins
// are loaded, which formIds a node without a persistentId still finds, and which
// are no player's spell. The fixed tree is checked three ways: the case's own
// expectations (spells kept, counts); links read back by spell name - every kept
// node's links, locks, bridges and root name the same spells as before, less the
// ones that went, and no formId names two spells; and a second pass over the
// fixed tree (the next load) changes nothing.
//
// Also FormIdText.h, the one reader and writer of formId text (hex, "0x" optional;
// ParseSpellOf reads an edit mode copy's "<formId>_dup_<n>" as its spell;
// junk, empty and more than 8 digits rejected), which NormalizeFormId uses.
//
// And EditorIdRematch.h: a node whose persistentId no longer resolves (its plugin
// renamed, merged or compacted) takes the one spell with its editor id in its school,
// and that spell's persistentId; one that lands on a spell another node of its school
// already has goes, the node that resolved by its own persistentId stays. Those cases
// are in tree-remap-check-rematch.cpp (the case types: tree-remap-check.h).
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "EditorIdRematch.h"
#include "TreeRemap.h"
#include "tree-remap-check.h"

#include <cstdio>
#include <format>
#include <map>
#include <optional>
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

    using TreeRemapCheck::Case;
    using TreeRemapCheck::FakeGame;
    using TreeRemapCheck::FakeSpell;

    std::string Norm(const std::string& id) { return TreeRemap::NormalizeFormId(id); }

    /// What JudgeNode does when a persistentId names nothing: the one spell with the node's editor id in its school
    const FakeSpell* Rematch(const FakeGame& game, const Json& node, const std::string& school)
    {
        const auto editorId = node.value("editorId", std::string());
        if (editorId.empty()) return nullptr;
        EditorIdRematch::CandidateIndex index;  // as IndexSpellsByEditorId builds it from the game: player's spells only
        for (const auto& spell : game.spells) {
            if (!game.notPlayerSpells.contains(FormIdText::Format(spell.formId))) index.Add(spell.editorId, spell.formId, spell.school);
        }
        const auto* candidates = index.Find(editorId);
        if (!candidates) return nullptr;
        const auto picked = EditorIdRematch::PickRematch(school, *candidates);
        if (!picked) return nullptr;
        for (const auto& spell : game.spells) {
            if (spell.formId == *picked) return &spell;
        }
        return nullptr;
    }

    TreeRemap::NodeVerdict Judge(const FakeGame& game, const Json& node, const std::string& school)
    {
        TreeRemap::NodeVerdict verdict;
        verdict.formId = node["formId"].get<std::string>();
        const auto pid = node.value("persistentId", std::string());
        bool found = false;
        if (pid.find('|') != std::string::npos) {
            verdict.hasPersistentId = true;
            const auto it = game.resolves.find(pid);
            if (it == game.resolves.end()) {
                if (const auto* spell = Rematch(game, node, school)) {
                    verdict.formId = FormIdText::Format(spell->formId);
                    verdict.rematched = true;
                    verdict.persistentId = spell->persistentId;
                    verdict.keep = true;   // the index holds player's spells only
                    return verdict;
                }
                const auto plugin = pid.substr(0, pid.find('|'));
                if (game.loadedPlugins.contains(plugin)) {
                    verdict.missingRecord = pid;
                } else {
                    verdict.missingPlugin = plugin;
                }
                return verdict;
            }
            verdict.formId = it->second;
            found = true;
        } else {
            const auto id = Norm(verdict.formId);
            found = game.legacyValid.contains(id);
            for (const auto& [p, resolved] : game.resolves) found = found || Norm(resolved) == id;
        }
        verdict.keep = found && !game.notPlayerSpells.contains(Norm(verdict.formId));
        return verdict;
    }

    // ------------------------------------------------------------------------
    // Reading links back by spell name
    // ------------------------------------------------------------------------

    struct Snapshot
    {
        std::map<std::string, std::string> names;  // formId -> spell name
        std::map<std::string, Json> nodes;         // "school/name" -> node
        std::set<std::string> spells;              // spell names
        std::map<std::string, std::string> roots;  // school -> root as a name
        std::vector<std::pair<std::string, std::string>> bridges;
        std::vector<std::string> clashes;          // formIds naming two different spells
    };

    std::string NameFor(const Snapshot& snap, const Json& id)
    {
        if (!id.is_string()) return "json:" + id.dump();
        const auto it = snap.names.find(Norm(id.get<std::string>()));
        return it != snap.names.end() ? it->second : "raw:" + Norm(id.get<std::string>());
    }

    Snapshot Take(const Json& tree)
    {
        Snapshot snap;
        for (const auto& [school, data] : tree["schools"].items()) {
            for (const auto& node : data["nodes"]) {
                if (!node.is_object() || !node.contains("formId") || !node["formId"].is_string()) continue;
                const auto id = Norm(node["formId"].get<std::string>());
                const auto name = node["name"].get<std::string>();
                const auto [it, added] = snap.names.emplace(id, name);
                if (!added && it->second != name) snap.clashes.push_back(id);
                snap.nodes[school + "/" + name] = node;
                snap.spells.insert(name);
            }
        }
        for (const auto& [school, data] : tree["schools"].items()) {
            if (data.contains("root")) snap.roots[school] = NameFor(snap, data["root"]);
        }
        if (tree.contains("bridges")) {
            for (const auto& b : tree["bridges"]) snap.bridges.emplace_back(NameFor(snap, b.value("from", Json())), NameFor(snap, b.value("to", Json())));
        }
        return snap;
    }

    /// A node's links (lists and locks) as names; `keep` filters out spells that went
    std::vector<std::string> LinkNames(const Json& node, const Snapshot& snap, const std::set<std::string>* keep)
    {
        std::vector<std::string> out;
        const auto add = [&](const std::string& key, const Json& id) {
            const auto name = NameFor(snap, id);
            const bool spell = name.rfind("raw:", 0) != 0 && name.rfind("json:", 0) != 0;
            if (keep && spell && !keep->contains(name)) return;
            out.push_back(key + "=" + name);
        };
        for (const char* key : TreeRemap::kLinkLists) {
            if (!node.contains(key)) continue;
            for (const auto& link : node[key]) add(key, link);
        }
        if (node.contains("locks")) {
            for (const auto& lock : node["locks"]) add("lock", lock.is_object() ? lock.value("nodeId", Json()) : lock);
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

    template <class Set>
    std::vector<std::string> List(const Set& set) { return { set.begin(), set.end() }; }

    bool LinksKept(const Snapshot& before, const Snapshot& after)
    {
        bool ok = true;
        for (const auto& [key, node] : after.nodes) {
            const auto was = LinkNames(before.nodes.at(key), before, &after.spells);
            const auto now = LinkNames(node, after, nullptr);
            if (was == now) continue;
            ok = false;
            std::printf("      %s: saved [%s], now [%s]\n", key.c_str(), Join(was).c_str(), Join(now).c_str());
        }
        return ok;
    }

    /// The saved root's spell if it stayed; the first spell left if it went; a root no node had stays
    bool RootsKept(const Snapshot& before, const Snapshot& after, const Json& tree)
    {
        bool ok = true;
        for (const auto& [school, rootWas] : before.roots) {
            const auto& nodes = tree["schools"][school]["nodes"];
            const bool wasNode = rootWas.rfind("raw:", 0) != 0;
            std::string expect = rootWas;
            if (wasNode && !after.spells.contains(rootWas) && !nodes.empty()) expect = nodes[0]["name"].get<std::string>();
            const auto it = after.roots.find(school);
            const std::string now = it != after.roots.end() ? it->second : "";
            // A root whose spell went with no spell left keeps its text, which names nothing now
            if (now == expect || (wasNode && nodes.empty() && now.rfind("raw:", 0) == 0)) continue;
            ok = false;
            std::printf("      %s root: %s, expected %s\n", school.c_str(), now.c_str(), expect.c_str());
        }
        return ok;
    }

    int RunCase(const Case& c)
    {
        std::printf("  %s\n", c.name);
        Checker check;
        Json tree = Json::parse(c.tree);
        const Json saved = tree;
        const auto judge = [&c](const Json& node, const std::string& school) { return Judge(c.game, node, school); };
        const Snapshot before = Take(tree);
        const auto report = TreeRemap::FixTree(tree, judge);
        // The panel gets the fixed tree exactly when it differs from the file (ValidateTreeForPanel)
        check.Check(report.Changed() == (tree != saved), std::format("Changed() is {}: the tree {}", report.Changed(),
            tree != saved ? "changed" : "is as saved"));
        const Snapshot after = Take(tree);

        const std::set<std::string> expectKept(c.expect.kept.begin(), c.expect.kept.end());
        check.Check(after.spells == expectKept, "spells kept: " + Join(List(after.spells)));
        check.Check(after.clashes.empty(), "no formId names two spells");
        check.Check(report.resolvedFromPersistent == c.expect.resolved && report.invalidNodes == c.expect.invalid &&
                        report.legacyCollisions == c.expect.collisions && report.legacyFollowed == c.expect.followed,
            std::format("counts: {} resolved, {} invalid, {} legacy collisions, {} legacy followed",
                report.resolvedFromPersistent, report.invalidNodes, report.legacyCollisions, report.legacyFollowed));
        check.Check(List(report.missingPlugins) == c.expect.missingPlugins &&
                        List(report.missingRecords) == c.expect.missingRecords,
            "missing plugins [" + Join(List(report.missingPlugins)) + "], records [" +
                Join(List(report.missingRecords)) + "]");
        for (const auto& [key, id] : c.expect.formIds) {
            const auto it = after.nodes.find(key);
            check.Check(it != after.nodes.end() && it->second["formId"] == id, key + " is " + id);
        }
        check.Check(report.rematched == c.expect.rematched &&
                        report.rematchClashes.size() == static_cast<std::size_t>(c.expect.rematchClashes),
            std::format("{} found again by editor id, {} dropped for a spell their school has", report.rematched,
                report.rematchClashes.size()));
        for (const auto& [key, id] : c.expect.persistentIds) {
            const auto it = after.nodes.find(key);
            check.Check(it != after.nodes.end() && it->second.value("persistentId", std::string()) == id,
                key + " has persistentId " + id);
        }
        check.Check(LinksKept(before, after), "every link, lock and prerequisite still names its own spell");

        std::vector<std::pair<std::string, std::string>> bridgesWas;
        for (const auto& b : before.bridges) {
            if (after.spells.contains(b.first) && after.spells.contains(b.second)) bridgesWas.push_back(b);
        }
        check.Check(after.bridges == bridgesWas, "bridges follow their spells; one to a spell that went is dropped");
        check.Check(RootsKept(before, after, tree), "school roots follow their spell, or the first spell left");

        Json again = tree;
        const auto second = TreeRemap::FixTree(again, judge);
        check.Check(again == tree && second.moved.empty() && second.removedFormIds.empty() && second.invalidNodes == 0 &&
                        second.rootChanges.empty(),
            "the next load changes nothing");
        return check.failed;
    }

    /// Trees whose shape is not a tree: left alone, counted as nothing
    int RunShapeCase()
    {
        std::printf("  shape: \"schools\" is not an object\n");
        Checker check;
        for (const char* text : { R"({"schools":[{"nodes":[{"formId":"0x1"}]}]})", R"({"schools":"x"})", R"([1,2])" }) {
            Json tree = Json::parse(text);
            const Json was = tree;
            const auto report = TreeRemap::FixTree(tree, [](const Json&, const std::string&) { return TreeRemap::NodeVerdict{}; });
            check.Check(!report.hasSchools && report.totalNodes == 0 && tree == was, std::string("left alone: ") + text);
        }
        return check.failed;
    }

    /// FormIdText (include/FormIdText.h), the one reader of formId text: hex with "0x" optional, nothing else
    int RunFormIdTextCase()
    {
        std::printf("  formId text: hex with or without 0x, any case; junk, empty and overflow rejected\n");
        Checker check;
        struct Expect
        {
            const char* text;
            std::optional<std::uint32_t> value;
        };
        const Expect cases[] = {
            { "0x0001ABCD", 0x0001ABCD },          // normal: what the scanner writes
            { "0001ABCD", 0x0001ABCD },            // no prefix (a hand-edited tree)
            { "0x0001abcd", 0x0001ABCD },          // lowercase digits
            { "0X0001ABCD", 0x0001ABCD },          // uppercase prefix
            { "00012345", 0x00012345 },            // hex, not octal (std::stoul base 0 read it as octal)
            { "12345678", 0x12345678 },            // hex, not decimal
            { "0xFE000800", 0xFE000800 },          // light plugin range, top bit set
            { "0x00000000", 0x00000000 },          // zero is a formId text (callers decide what 0 means)
            { "0x0001ABCD_dup_17", std::nullopt },  // an edit mode copy is not its original
            { "0x12zz", std::nullopt },            // trailing junk
            { " 0x1", std::nullopt },              // leading space
            { "-1", std::nullopt },                // a sign
            { "+1", std::nullopt },
            { "0x", std::nullopt },                // prefix only
            { "", std::nullopt },                  // empty
            { "0x1FFFFFFFF", std::nullopt },       // overflow: 9 digits
            { "000000001", std::nullopt },         // 9 digits even when the value fits
        };
        for (const auto& c : cases) {
            const auto got = FormIdText::Parse(c.text);
            check.Check(got == c.value, std::format("Parse(\"{}\") is {}", c.text,
                c.value ? std::format("0x{:08X}", *c.value) : std::string("rejected")));
        }
        // ParseSpellOf: an edit mode copy's node id ("<formId>_dup_<n>") names its spell, as the
        // panel's getCanonicalFormId reads it; a plain formId as Parse; junk still rejected
        const Expect spellOf[] = {
            { "0x0001ABCD_dup_1729000000000", 0x0001ABCD },  // a saved copy (EditMode.spawnSpell)
            { "0x0001ABCD", 0x0001ABCD },                    // no copy: Parse
            { "0001abcd_dup_", 0x0001ABCD },                 // edge: no number after _dup_, no 0x
            { "_dup_17", std::nullopt },                     // nothing before _dup_
            { "0x12zz_dup_1", std::nullopt },                // junk before _dup_
            { "0x0001ABCD_copy", std::nullopt },             // another suffix is junk
        };
        for (const auto& c : spellOf) {
            check.Check(FormIdText::ParseSpellOf(c.text) == c.value, std::format("ParseSpellOf(\"{}\") is {}", c.text,
                c.value ? std::format("0x{:08X}", *c.value) : std::string("rejected")));
        }
        check.Check(FormIdText::Format(0x0001ABCD) == "0x0001ABCD" && FormIdText::Format(0) == "0x00000000" &&
                        FormIdText::Format(0xFE000800) == "0xFE000800",
            "Format writes 0x and 8 uppercase digits");
        for (const auto& c : cases) {
            if (!c.value) continue;
            check.Check(FormIdText::Format(*FormIdText::Parse(c.text)) == TreeRemap::NormalizeFormId(c.text),
                std::format("NormalizeFormId(\"{}\") is Format(Parse())", c.text));
        }
        check.Check(TreeRemap::NormalizeFormId("0x0001ABCD_dup_17") == "0x0001ABCD_dup_17",
            "NormalizeFormId leaves a text that is no formId as it is");
        return check.failed;
    }

    // ------------------------------------------------------------------------
    // The cases (trees as JSON text: every link holds the saved formIds)
    // ------------------------------------------------------------------------

    std::vector<Case> Cases()
    {
        return {
            // Same load order, ids only spelled otherwise: written back as 0x%08X, so the tree changed
            { "formIds in lower case or unpadded, nothing moved: the text is rewritten",
                R"({"schools":{"Illusion":{"root":"0x0500A800","nodes":[
                    {"name":"Calm","formId":"0x0500a800","persistentId":"Calm.esp|0x00A800","children":["0x5000801"],
                     "locks":[{"nodeId":"0x5000801"}]},
                    {"name":"Fury","formId":"0x5000801","persistentId":"Calm.esp|0x000801","prerequisites":["0x0500a800"]}]}},
                  "bridges":[{"from":"0x0500a800","to":"0x5000801"}]})",
                { { { "Calm.esp|0x00A800", "0x0500A800" }, { "Calm.esp|0x000801", "0x05000801" } } },
                { { "Calm", "Fury" }, 0, 0, 0, 0, {}, {},
                    { { "Illusion/Calm", "0x0500A800" }, { "Illusion/Fury", "0x05000801" } } } },

            { "shift: light plugin A removed, B and C move down one index (normal)",
                R"({"schools":{"Destruction":{"root":"0xFE906800","nodes":[
                    {"name":"Alpha","formId":"0xFE905800","persistentId":"A.esl|0x000800","prerequisites":["0xFE906800"]},
                    {"name":"Bravo","formId":"0xFE906800","persistentId":"B.esl|0x000800","children":["0xFE907800","0xFE905800"]},
                    {"name":"Charlie","formId":"0xFE907800","persistentId":"C.esl|0x000800","prerequisites":["0xFE906800"],
                     "hardPrereqs":["0xFE906800"],"softPrereqs":["0xFE905800"],"locks":[{"nodeId":"0xFE906800","type":"x"}]},
                    {"name":"Delta","formId":"0x01000D62","persistentId":"Skyrim.esm|0x000D62","children":["0xFE907800"]}]}},
                  "bridges":[{"from":"0xFE906800","to":"0x01000D62"},{"from":"0xFE905800","to":"0x01000D62"}]})",
                { { { "B.esl|0x000800", "0xFE905800" }, { "C.esl|0x000800", "0xFE906800" }, { "Skyrim.esm|0x000D62", "0x01000D62" } } },
                { { "Bravo", "Charlie", "Delta" }, 2, 1, 0, 0, { "A.esl" }, {} } },

            { "swap: Arclight and Bloodmoon trade light indices (edge)",
                R"({"schools":{
                    "Destruction":{"root":"0xFE906838","nodes":[
                      {"name":"Arclight","formId":"0xFE906838","persistentId":"Arclight.esp|0x000838","children":["0x02000801"],"softPrereqs":["0xFE907838"]},
                      {"name":"ArcChild","formId":"0x02000801","persistentId":"Vanilla.esm|0x000801","prerequisites":["0xFE906838"]}]},
                    "Conjuration":{"root":"0xFE907838","nodes":[
                      {"name":"Bloodmoon","formId":"0xFE907838","persistentId":"Bloodmoon.esp|0x000838","children":["0x02000802"]},
                      {"name":"BloodChild","formId":"0x02000802","persistentId":"Vanilla.esm|0x000802","prerequisites":["0xFE907838"],"locks":[{"nodeId":"0xFE906838"}]}]}},
                  "bridges":[{"from":"0xFE906838","to":"0xFE907838"}]})",
                { { { "Arclight.esp|0x000838", "0xFE907838" }, { "Bloodmoon.esp|0x000838", "0xFE906838" },
                    { "Vanilla.esm|0x000801", "0x02000801" }, { "Vanilla.esm|0x000802", "0x02000802" } } },
                { { "Arclight", "ArcChild", "Bloodmoon", "BloodChild" }, 2, 0, 0, 0, {}, {} } },

            { "slot taken: Arclight's spell gone, Bloodmoon's moved onto its saved formId (2026-10-05)",
                R"({"schools":{"Destruction":{"root":"0xFE906838","nodes":[
                    {"name":"Arclight","formId":"0xFE906838","persistentId":"Arclight.esp|0x000838","children":["0xFE907838"]},
                    {"name":"Bloodmoon","formId":"0xFE907838","persistentId":"Bloodmoon.esp|0x000838","prerequisites":["0xFE906838"],"children":["0x02000803"]},
                    {"name":"Echo","formId":"0x02000803","persistentId":"Vanilla.esm|0x000803","prerequisites":["0xFE907838"],"softPrereqs":["0xFE906838"]}]}}})",
                { { { "Bloodmoon.esp|0x000838", "0xFE906838" }, { "Vanilla.esm|0x000803", "0x02000803" } } },
                { { "Bloodmoon", "Echo" }, 1, 1, 0, 0, { "Arclight.esp" }, {} } },

            { "legacy nodes (failure path): still found, gone, on a resolved spell's formId, no formId, not an object",
                R"({"schools":{"Alteration":{"root":"0xFE905800","nodes":[
                    {"name":"OldA","formId":"0xFE905800","children":["0x03000900"]},
                    {"name":"Moved","formId":"0xFE906800","persistentId":"B.esl|0x000800","children":["0xFE905800"]},
                    {"name":"Stays","formId":"0x03000900","prerequisites":["0xFE905800","0xFE906800",42,null],"children":["0x03000901"]},
                    {"name":"Gone","formId":"0x03000901","prerequisites":["0x03000900"]},
                    {"name":"NoFormId"}, 7, "0x03000902"]}}})",
                { { { "B.esl|0x000800", "0xFE905800" } }, {}, { "0x03000900" } },
                { { "Moved", "Stays" }, 1, 5, 1, 0, {}, {} } },

            { "legacy copy follows its persistentId node: moved in one school, gone in another",
                R"({"schools":{
                    "Destruction":{"root":"0x04000100","nodes":[
                      {"name":"Firebolt","formId":"0x04000100","persistentId":"Fire.esp|0x000100","children":["0x04000200"]},
                      {"name":"Gone","formId":"0x04000200","persistentId":"Fire.esp|0x000200","prerequisites":["0x04000100"]}]},
                    "Conjuration":{"root":"0x04000100","nodes":[
                      {"name":"Firebolt","formId":"0x04000100","children":["0x04000200"]},
                      {"name":"Gone","formId":"0x04000200"},
                      {"name":"Atronach","formId":"0x01000300","persistentId":"Skyrim.esm|0x000300","prerequisites":["0x04000100","0x04000200"]}]}}})",
                { { { "Fire.esp|0x000100", "0x05000100" }, { "Skyrim.esm|0x000300", "0x01000300" } }, { "Fire.esp" },
                    { "0x04000100", "0x04000200" } },
                { { "Firebolt", "Atronach" }, 2, 2, 0, 2, {}, { "Fire.esp|0x000200" },
                    { { "Conjuration/Firebolt", "0x05000100" }, { "Destruction/Firebolt", "0x05000100" } } } },

            { "same spell kept in one school, dropped in another (a broken copy)",
                R"({"schools":{
                    "Restoration":{"root":"0x06000100","nodes":[
                      {"name":"Heal","formId":"0x06000100","persistentId":"Heal.esp|0x000100","children":["0x01000500"]}]},
                    "Alteration":{"root":"0x06000100","nodes":[
                      {"name":"Heal","formId":"0x06000100","persistentId":"Old.esp|0x000100"},
                      {"name":"Ward","formId":"0x01000500","persistentId":"Skyrim.esm|0x000500","prerequisites":["0x06000100"]}]}}})",
                { { { "Heal.esp|0x000100", "0x07000100" }, { "Skyrim.esm|0x000500", "0x01000500" } } },
                { { "Heal", "Ward" }, 1, 1, 0, 0, { "Old.esp" }, {} } },

            { "links to removed spells: plugin gone, record gone, not a player's spell, link to no node",
                R"({"schools":{"Restoration":{"root":"0x05000100","nodes":[
                    {"name":"RemovedRoot","formId":"0x05000100","persistentId":"Gone.esp|0x000100","children":["0x01000200","0x01000300"]},
                    {"name":"Heal","formId":"0x01000200","persistentId":"Kept.esp|0x000200","prerequisites":["0x05000100"],
                     "children":["0x01000300","0x01000400"],"softPrereqs":["0x0A00ABCD"],
                     "locks":[{"nodeId":"0x05000100"},{"nodeId":"0x01000300"},{"nodeId":"0x01000400"},{"type":"noId"}]},
                    {"name":"RecordGone","formId":"0x01000300","persistentId":"Kept.esp|0x000300","prerequisites":["0x01000200"]},
                    {"name":"Ability","formId":"0x01000400","persistentId":"Kept.esp|0x000400","prerequisites":["0x01000200"]}]}},
                  "bridges":[{"from":"0x01000200","to":"0x05000100"},{"from":"0x01000200","to":"0x01000200"},{"from":"0x01000200"}]})",
                { { { "Kept.esp|0x000200", "0x02000200" }, { "Kept.esp|0x000400", "0x02000400" } }, { "Kept.esp" }, {},
                    { "0x02000400" } },
                { { "Heal" }, 1, 3, 0, 0, { "Gone.esp" }, { "Kept.esp|0x000300" } } },

            { "formIds in another case and a root no node has",
                R"({"schools":{"Illusion":{"root":"0x0900ffff","nodes":[
                    {"name":"Calm","formId":"0xfe905800","persistentId":"Calm.esl|0x800","children":["0xFE906800"]},
                    {"name":"Fear","formId":"0xfe906800","children":["0xfe905800"]},
                    {"name":"Fury","formId":"0x01000011","persistentId":"Skyrim.esm|0x000011","prerequisites":["0XFE905800"]}]}}})",
                { { { "Calm.esl|0x800", "0xFE906800" }, { "Skyrim.esm|0x000011", "0x01000011" } } },
                { { "Calm", "Fury" }, 1, 1, 1, 0, {}, {} } },

            { "same load order: nothing changes",
                R"({"schools":{"Illusion":{"root":"0x01000010","nodes":[
                    {"name":"Calm","formId":"0x01000010","persistentId":"Skyrim.esm|0x000010","children":["0x01000011"]},
                    {"name":"Fury","formId":"0x01000011","persistentId":"Skyrim.esm|0x000011","prerequisites":["0x01000010"]}]}}})",
                { { { "Skyrim.esm|0x000010", "0x01000010" }, { "Skyrim.esm|0x000011", "0x01000011" } } },
                { { "Calm", "Fury" }, 0, 0, 0, 0, {}, {} } },
        };
    }

    int SelfTest()
    {
        constexpr std::size_t kExtraCases = 3;  // RunShapeCase, RunFormIdTextCase and RunRematchCase (the other file)
        int failedCases = RunShapeCase() > 0 ? 1 : 0;
        if (RunFormIdTextCase() > 0) ++failedCases;
        if (TreeRemapCheck::RunRematchCase() > 0) ++failedCases;
        auto cases = TreeRemapCheck::RematchCases();   // tree-remap-check-rematch.cpp, first as before
        for (auto& c : Cases()) cases.push_back(std::move(c));
        for (const auto& c : cases) {
            if (RunCase(c) > 0) ++failedCases;
        }
        std::printf("self-test: %d of %zu cases failed\n", failedCases, cases.size() + kExtraCases);
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
