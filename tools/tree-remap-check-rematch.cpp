// tree-remap-check, its editor id rematch part (tree-remap-check.cpp runs these):
// EditorIdRematch.h on its own, and saved trees whose node's persistentId no longer
// resolves (its plugin renamed, merged or compacted): it takes the one spell with its
// editor id in its school, and that spell's persistentId; one that lands on a spell
// another node of its school already has goes, the node that resolved by its own
// persistentId stays.

#include "EditorIdRematch.h"
#include "tree-remap-check.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct Checker
    {
        int failed = 0;
        void Check(bool ok, const std::string& what)
        {
            if (!ok) ++failed;
            std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        }
    };
}

namespace TreeRemapCheck
{
    /// EditorIdRematch (include/EditorIdRematch.h): the one candidate in the node's school, else none
    int RunRematchCase()
    {
        std::printf("  editor id rematch: one spell in the school, else none\n");
        Checker check;
        using Candidates = std::vector<std::pair<std::uint32_t, std::string>>;
        check.Check(EditorIdRematch::PickRematch("Destruction", Candidates{ { 0xFE000800, "Destruction" } }) == 0xFE000800u,
            "rematch: one match in the school");
        check.Check(!EditorIdRematch::PickRematch("Destruction",
                        Candidates{ { 0xFE000800, "Destruction" }, { 0x05000800, "Destruction" } }).has_value(),
            "two matches - none");
        check.Check(!EditorIdRematch::PickRematch("Destruction", Candidates{ { 0xFE000800, "Illusion" } }).has_value(),
            "other school - none");
        check.Check(!EditorIdRematch::PickRematch("Destruction", Candidates{}).has_value(), "no editor id - none");
        // The index folds case (ASCII) as the engine's editor id lookup did
        EditorIdRematch::CandidateIndex index;
        index.Add("SpellFirebolt", 0xFE000800, "Destruction");
        index.Add("SpellIcebolt", 0x05000801, "Destruction");
        index.Add("SPELLICEBOLT", 0x07000801, "Destruction");
        const auto* firebolt = index.Find("spellFIREBOLT");
        check.Check(firebolt && EditorIdRematch::PickRematch("Destruction", *firebolt) == 0xFE000800u,
            "an editor id that differs only in case matches");
        const auto* icebolt = index.Find("SpellIcebolt");
        check.Check(icebolt && !EditorIdRematch::PickRematch("Destruction", *icebolt).has_value(),
            "case variants of one editor id are two matches - none");
        check.Check(index.Find("SpellSpark") == nullptr && index.Size() == 2, "an editor id no spell has finds nothing");
        // Edge: the same spell listed twice is one match; one in another school beside it does not count
        check.Check(EditorIdRematch::PickRematch("Destruction",
                        Candidates{ { 0xFE000800, "Destruction" }, { 0xFE000800, "Destruction" }, { 0x05000800, "Illusion" } }) ==
                        0xFE000800u,
            "the same spell twice is one match; another school's is not counted");
        return check.failed;
    }

    std::vector<Case> RematchCases()
    {
        return {
            { "renamed plugin: found again by editor id with its new persistentId; two in the school, another "
              "school's, no editor id: not taken",
                R"({"schools":{"Destruction":{"root":"0x05000800","nodes":[
                    {"name":"Firebolt","formId":"0x05000800","persistentId":"Old.esp|0x000800","editorId":"SpellFirebolt",
                     "children":["0x05000801"]},
                    {"name":"Icebolt","formId":"0x05000801","persistentId":"Old.esp|0x000801","editorId":"SpellIcebolt",
                     "prerequisites":["0x05000800"]},
                    {"name":"Spark","formId":"0x05000802","persistentId":"Old.esp|0x000802","editorId":"SpellSpark"},
                    {"name":"Flame","formId":"0x05000803","persistentId":"Old.esp|0x000803"}]}}})",
                { { { "New.esp|0x000800", "0x06000800" } }, {}, {}, {},
                    { { "spellfirebolt", 0x06000800, "Destruction", "New.esp|0x000800" },
                        { "SpellIcebolt", 0x06000801, "Destruction", "New.esp|0x000801" },
                        { "SPELLICEBOLT", 0x07000801, "Destruction", "Fork.esp|0x000801" },
                        { "SpellSpark", 0x06000802, "Illusion", "New.esp|0x000802" } } },
                { { "Firebolt" }, 0, 3, 0, 0, { "Old.esp" }, {}, { { "Destruction/Firebolt", "0x06000800" } }, 1, 0,
                    { { "Destruction/Firebolt", "New.esp|0x000800" } } } },

            { "renamed plugin at the same load index: the formId stays, the node takes the new persistentId",
                R"({"schools":{"Alteration":{"root":"0x05000810","nodes":[
                    {"name":"Oakflesh","formId":"0x05000810","persistentId":"Old.esp|0x000810","editorId":"SpellOakflesh"}]}}})",
                { { { "New.esp|0x000810", "0x05000810" } }, {}, {}, {},
                    { { "SpellOakflesh", 0x05000810, "Alteration", "New.esp|0x000810" } } },
                { { "Oakflesh" }, 0, 0, 0, 0, {}, {}, { { "Alteration/Oakflesh", "0x05000810" } }, 1, 0,
                    { { "Alteration/Oakflesh", "New.esp|0x000810" } } } },

            // Restoration's copy has no persistentId and was saved under the A.esp node's formId: it follows
            // that node's verdict before the clash, which only concerns Destruction
            { "a legacy copy in another school follows a rematched node that its own school dropped",
                R"({"schools":{"Destruction":{"root":"0x06000800","nodes":[
                    {"name":"Firebolt of A.esp","formId":"0x05000800","persistentId":"A.esp|0x000800","editorId":"SpellFirebolt"},
                    {"name":"Firebolt","formId":"0x06000800","persistentId":"B.esp|0x000800","editorId":"SpellFirebolt"}]},
                  "Restoration":{"root":"0x05000800","nodes":[
                    {"name":"Firebolt","formId":"0x05000800"}]}}})",
                { { { "B.esp|0x000800", "0x06000800" } }, {}, {}, {},
                    { { "SpellFirebolt", 0x06000800, "Destruction", "B.esp|0x000800" } } },
                { { "Firebolt" }, 1, 1, 0, 1, {}, {},
                    { { "Destruction/Firebolt", "0x06000800" }, { "Restoration/Firebolt", "0x06000800" } }, 0, 1, {} } },

            // The names differ only so the check can tell the two nodes apart; in a real tree both say "Firebolt"
            { "a rematched node on a spell its school already has goes; the persistentId node stays",
                R"({"schools":{"Destruction":{"root":"0x05000800","nodes":[
                    {"name":"Firebolt of A.esp","formId":"0x05000800","persistentId":"A.esp|0x000800","editorId":"SpellFirebolt",
                     "children":["0x05000900"]},
                    {"name":"Firebolt","formId":"0x06000800","persistentId":"B.esp|0x000800","editorId":"SpellFirebolt"},
                    {"name":"Ember","formId":"0x05000900","persistentId":"B.esp|0x000900","prerequisites":["0x05000800"]}]}}})",
                { { { "B.esp|0x000800", "0x06000800" }, { "B.esp|0x000900", "0x06000900" } }, {}, {}, {},
                    { { "SpellFirebolt", 0x06000800, "Destruction", "B.esp|0x000800" } } },
                { { "Firebolt", "Ember" }, 1, 1, 0, 0, {}, {},
                    { { "Destruction/Firebolt", "0x06000800" }, { "Destruction/Ember", "0x06000900" } }, 0, 1,
                    { { "Destruction/Firebolt", "B.esp|0x000800" } } } },

            // Restoration has the target spell's own node: the copy would be a second node of one formId there
            // (the last one's empty prerequisites would win in game), so it goes and the real node's stay
            { "a legacy copy of a clash-dropped node goes when its own school already has the spell",
                R"({"schools":{"Destruction":{"root":"0x06000800","nodes":[
                    {"name":"Firebolt of A.esp","formId":"0x05000800","persistentId":"A.esp|0x000800","editorId":"SpellFirebolt"},
                    {"name":"Firebolt","formId":"0x06000800","persistentId":"B.esp|0x000800","editorId":"SpellFirebolt"}]},
                  "Restoration":{"root":"0x01000100","nodes":[
                    {"name":"Heal","formId":"0x01000100","persistentId":"Skyrim.esm|0x000100","children":["0x06000800"]},
                    {"name":"Firebolt copy","formId":"0x05000800"},
                    {"name":"Firebolt","formId":"0x06000800","persistentId":"B.esp|0x000800","prerequisites":["0x01000100"]}]}}})",
                { { { "B.esp|0x000800", "0x06000800" }, { "Skyrim.esm|0x000100", "0x01000100" } }, {}, {}, {},
                    { { "SpellFirebolt", 0x06000800, "Destruction", "B.esp|0x000800" } } },
                { { "Firebolt", "Heal" }, 0, 2, 1, 0, {}, {},
                    { { "Destruction/Firebolt", "0x06000800" }, { "Restoration/Firebolt", "0x06000800" } }, 0, 1, {} } },

            // Only player's spells are candidates (IndexSpellsByEditorId): an NPC's spell with the same
            // editor id is no second match, and one that is the only match leaves the plugin missing
            { "rematch: an NPC's spell with the editor id is no candidate - the player's one is taken, and a node "
              "with only an NPC's match is a missing plugin",
                R"({"schools":{"Destruction":{"root":"0x01000100","nodes":[
                    {"name":"Heal","formId":"0x01000100","persistentId":"Skyrim.esm|0x000100",
                     "children":["0x05000800","0x05000802"]},
                    {"name":"Firebolt","formId":"0x05000800","persistentId":"Old.esp|0x000800","editorId":"SpellFirebolt",
                     "prerequisites":["0x01000100"]},
                    {"name":"Spark","formId":"0x05000802","persistentId":"Old.esp|0x000802","editorId":"SpellSpark",
                     "prerequisites":["0x01000100"]}]}}})",
                { { { "Skyrim.esm|0x000100", "0x01000100" }, { "New.esp|0x000800", "0x06000800" } }, {}, {},
                    { "0x07000800", "0x07000802" },
                    { { "SpellFirebolt", 0x06000800, "Destruction", "New.esp|0x000800" },
                        { "SpellFirebolt", 0x07000800, "Destruction", "Npc.esp|0x000800" },
                        { "SpellSpark", 0x07000802, "Destruction", "Npc.esp|0x000802" } } },
                { { "Heal", "Firebolt" }, 0, 1, 0, 0, { "Old.esp" }, {}, { { "Destruction/Firebolt", "0x06000800" } }, 1, 0,
                    { { "Destruction/Firebolt", "New.esp|0x000800" } } } },
        };
    }
}
