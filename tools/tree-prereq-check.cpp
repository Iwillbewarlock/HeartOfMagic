// tree-prereq-check - offline check of the tree prerequisites game start applies
// (plugins/spelllearning/include/TreePrereqEntries.h): the array the panel's fast
// path sends to SetTreePrerequisites, made with no panel.
//
//   tree-prereq-check --self-test      made-up trees, expected arrays written by hand from the JS rules
//   tree-prereq-check <tree.json>      the array for that tree's text, one line of JSON
//                                      ("null" and the reason on stderr when nothing is sent)
//   tree-prereq-check --dump-cases     the self-test's cases as JSON ([{name, tree, outcome,
//                                      entries}], the tree as its text): the panel's own code
//                                      runs them in modules/treePrereqParityTest.js, whose copy
//                                      is test-data/treePrereqCases.json
//
// Exit 0 when every case agrees (or the file's array was printed), 1 on a disagreement,
// 2 on a bad argument or a file that cannot be read, 3 when the panel would send nothing.

#include "TreePrereqEntries.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitMismatch = 1;
    constexpr int kExitUsage = 2;
    constexpr int kExitNothingSent = 3;

    using TreePrereqEntries::Outcome;
    using OJson = TreePrereqEntries::OJson;

    struct Case
    {
        const char* name;
        std::string_view tree;
        Outcome outcome;
        std::string_view entries;  ///< Built: the expected array
    };

    // Entries are {formId, hardPrereqs, softPrereqs, softNeeded}, in that key order (as the panel builds them)
    constexpr Case kCases[] = {
        // ---- normal ----
        { "normal tree",
          R"({"trustPrereqs":true,"schools":{"Destruction":{"root":"0x1","nodes":[
              {"formId":"0x1","children":["0x2"]},
              {"formId":"0x2","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x3","hardPrereqs":["0x2"],"softPrereqs":["0x1","0x4"],"softNeeded":1},
              {"formId":"0x4","prerequisites":["0x1"]}]}}})",
          Outcome::Built,
          R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x2","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x3","hardPrereqs":["0x2"],"softPrereqs":["0x1","0x4"],"softNeeded":1},
              {"formId":"0x4","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0}])" },

        // ---- roots ----
        { "root by isRoot and by the school's root",
          R"({"trustPrereqs":true,"schools":{"A":{"root":"0xB","nodes":[
              {"formId":"0xA","isRoot":true,"hardPrereqs":["0xB"],"softPrereqs":["0xC"],"softNeeded":1},
              {"formId":"0xB","prerequisites":["0xA"],"hardPrereqs":["0xA"]},
              {"formId":"0xC","isRoot":0,"hardPrereqs":["0xA"]}]}}})",
          Outcome::Built,
          R"([{"formId":"0xA","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0xB","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0xC","hardPrereqs":["0xA"],"softPrereqs":[],"softNeeded":0}])" },
        { "root compared as an exact string",
          R"({"trustPrereqs":true,"schools":{"A":{"root":"0x0001","nodes":[{"formId":"0x1","hardPrereqs":["0x2"]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2"],"softPrereqs":[],"softNeeded":0}])" },

        // ---- fast path test ----
        { "fast path by x (null counts) without trustPrereqs",
          R"({"schools":{"A":{"nodes":[{"formId":"0x1","x":null,"hardPrereqs":["0x2"]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2"],"softPrereqs":[],"softNeeded":0}])" },
        { "slow path: the first node has no x (the second's is not looked at)",
          R"({"schools":{"A":{"nodes":[{"formId":"0x1"},{"formId":"0x2","x":5}]}}})", Outcome::SlowPath, "" },
        { "slow path: the first school is empty (the next is not looked at)",
          R"({"schools":{"A":{"nodes":[]},"B":{"nodes":[{"formId":"0x2","x":5}]}}})", Outcome::SlowPath, "" },
        { "slow path: trustPrereqs false",
          R"({"trustPrereqs":false,"schools":{"A":{"nodes":[{"formId":"0x1"}]}}})", Outcome::SlowPath, "" },

        // ---- the fallback from prerequisites ----
        { "fallback: self out, first hard, rest soft, need 1",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","prerequisites":["0x1","0x2","0x3"],"softNeeded":5}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2"],"softPrereqs":["0x3"],"softNeeded":1}])" },
        { "fallback: a repeated prerequisite deduplicated by repair",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1","prerequisites":["0x2","0x2","0x3"]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2"],"softPrereqs":["0x3"],"softNeeded":1}])" },
        { "fallback: one prerequisite is hard, softNeeded clamped to the empty soft list",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1","prerequisites":["0x2"],"softNeeded":3}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2"],"softPrereqs":[],"softNeeded":0}])" },
        { "no fallback when soft has entries",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","prerequisites":["0x2","0x3"],"softPrereqs":["0x4"],"softNeeded":1}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":["0x4"],"softNeeded":1}])" },

        // ---- repair and locks ----
        { "lock repair, object lock: softNeeded down by one",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","hardPrereqs":["0x5"],"softPrereqs":["0x5","0x6"],"softNeeded":2,"locks":[{"nodeId":"0x5","type":"x"}]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x5"],"softPrereqs":["0x6"],"softNeeded":1}])" },
        { "lock repair, string lock: softNeeded 1 to 0",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","hardPrereqs":["0x5"],"softPrereqs":["0x5","0x6"],"softNeeded":1,"locks":["0x5"]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x5"],"softPrereqs":["0x6"],"softNeeded":0}])" },
        { "repair without a lock: softNeeded kept, held to the soft list",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","hardPrereqs":["0x5","0x5"],"softPrereqs":["0x5","0x6"],"softNeeded":2,"locks":[{"nodeId":"0x9"}]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x5"],"softPrereqs":["0x6"],"softNeeded":1}])" },
        { "negative softNeeded to 0, fractional kept",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","softPrereqs":["0x2","0x3"],"softNeeded":-2},
              {"formId":"0x4","softPrereqs":["0x2","0x3"],"softNeeded":1.5}]}}})",
          Outcome::Built,
          R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":["0x2","0x3"],"softNeeded":0},
              {"formId":"0x4","hardPrereqs":[],"softPrereqs":["0x2","0x3"],"softNeeded":1.5}])" },
        { "ids compared as exact strings (no hex normalising)",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","hardPrereqs":["0x2","0x01"],"softPrereqs":["0X2","0x02"],"softNeeded":2}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2","0x01"],"softPrereqs":["0X2","0x02"],"softNeeded":2}])" },
        { "self id out of hard and soft",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x2","hardPrereqs":["0x2","0x1"],"softPrereqs":["0x2"],"softNeeded":1}]}}})",
          Outcome::Built, R"([{"formId":"0x2","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0}])" },

        // ---- edit mode copies ----
        { "_dup_ copy of a spell that has its node (either order): not sent",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x7","hardPrereqs":["0x1"]},{"formId":"0x7_dup_1","hardPrereqs":["0x2"]},
              {"formId":"0x8_dup_1","hardPrereqs":["0x2"]},{"formId":"0x8","hardPrereqs":["0x1"]}]}}})",
          Outcome::Built,
          R"([{"formId":"0x7","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x8","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0}])" },
        { "_dup_ copies without their spell: the first one, as the spell",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x9_dup_1","hardPrereqs":["0x1"]},{"formId":"0x9_dup_2","hardPrereqs":["0x2"]}]}}})",
          Outcome::Built, R"([{"formId":"0x9","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0}])" },
        { "_dup_ in the lists: made the spell's, once",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","hardPrereqs":["0xB_dup_3","0xB"],"softPrereqs":["0xC","0xC_dup_1"],"softNeeded":1}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0xB"],"softPrereqs":["0xC"],"softNeeded":1}])" },

        // ---- schools ----
        { "same formId in two schools: both sent, in school order (as written)",
          R"({"trustPrereqs":true,"schools":{"Zeta":{"nodes":[{"formId":"0x5","hardPrereqs":["0x1"]}]},
              "Alpha":{"nodes":[{"formId":"0x5","hardPrereqs":["0x2"]}]}}})",
          Outcome::Built,
          R"([{"formId":"0x5","hardPrereqs":["0x1"],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x5","hardPrereqs":["0x2"],"softPrereqs":[],"softNeeded":0}])" },
        { "array-index school names first, ascending",
          R"({"trustPrereqs":true,"schools":{"B":{"nodes":[{"formId":"0xB"}]},"2":{"nodes":[{"formId":"0x2"}]},
              "01":{"nodes":[{"formId":"0x01"}]},"1":{"nodes":[{"formId":"0x1"}]}}})",
          Outcome::Built,
          R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x2","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0xB","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x01","hardPrereqs":[],"softPrereqs":[],"softNeeded":0}])" },

        // ---- what the JS leaves out ----
        { "non-string list entries left out",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"formId":"0x1","hardPrereqs":["0x2",5,null,{"a":1},["0x9"],"0x3"],"softPrereqs":[true,"0x4"],"softNeeded":1}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2","0x3"],"softPrereqs":["0x4"],"softNeeded":1}])" },
        { "nodes without a formId (missing, empty, null, a number node) not sent",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[
              {"hardPrereqs":["0x2"]},{"formId":"","hardPrereqs":["0x2"]},{"formId":null},7,{"formId":"0x1"}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":[],"softNeeded":0}])" },
        { "Object.prototype names are already in the panel's sets",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1","hardPrereqs":["constructor","0x2","toString"]}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":["0x2"],"softPrereqs":[],"softNeeded":0}])" },
        { "a school with no node list: nothing from it",
          R"({"trustPrereqs":true,"schools":{"A":{"root":"0x1"},"B":{"nodes":"abc"},"C":{"nodes":[{"formId":"0x1"}]}}})",
          Outcome::Built, R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":[],"softNeeded":0}])" },

        // ---- the panel sends nothing ----
        { "empty schools", R"({"trustPrereqs":true,"schools":{}})", Outcome::NoSchools, "" },
        { "no schools", R"({"trustPrereqs":true})", Outcome::NoSchools, "" },
        { "schools an empty array", R"({"trustPrereqs":true,"schools":[]})", Outcome::NoSchools, "" },
        { "schools a number", R"({"trustPrereqs":true,"schools":5})", Outcome::NoSchools, "" },
        { "tree not an object", R"([{"schools":{"A":{}}}])", Outcome::NoSchools, "" },
        { "a null node", R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1"},null]}}})", Outcome::PanelThrows, "" },
        { "a null school", R"({"trustPrereqs":true,"schools":{"A":null}})", Outcome::PanelThrows, "" },
        { "a list that is a string", R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1","hardPrereqs":"0x2"}]}}})",
          Outcome::PanelThrows, "" },
        { "children a string (on a node without formId too)",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"children":"abc"}]}}})", Outcome::PanelThrows, "" },
        { "a school named hasOwnProperty (fast path by trustPrereqs)",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1"}]},"hasOwnProperty":{"nodes":[]}}})",
          Outcome::PanelThrows, "" },
        { "a school named hasOwnProperty (the fast path test calls it too)",
          R"({"schools":{"hasOwnProperty":{"nodes":[{"formId":"0x1","x":0}]}}})", Outcome::PanelThrows, "" },
        { "trustPrereqs an object with its own toString (logged first)",
          R"({"trustPrereqs":{"toString":1},"schools":{"A":{"nodes":[{"formId":"0x1"}]}}})", Outcome::PanelThrows, "" },
        { "a school's root with its own toString",
          R"({"trustPrereqs":true,"schools":{"A":{"root":{"toString":"0x1"},"nodes":[{"formId":"0x1"}]}}})",
          Outcome::PanelThrows, "" },
        { "a school's spokeAngle an array holding a toString object",
          R"({"trustPrereqs":true,"schools":{"A":{"spokeAngle":[1,[{"toString":2}]],"nodes":[{"formId":"0x1"}]}}})",
          Outcome::PanelThrows, "" },
        { "a logged node's x with its own toString (third node)",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1"},{"formId":"0x2"},{"x":{"toString":0}}]}}})",
          Outcome::PanelThrows, "" },
        { "toString on the fourth node, valueOf anywhere: printed fine",
          R"({"trustPrereqs":true,"schools":{"A":{"color":{"valueOf":1},"nodes":[
              {"formId":"0x1","name":{"valueOf":2}},{"formId":"0x2"},{"formId":"0x3"},{"formId":"0x4","tier":{"toString":4}}]}}})",
          Outcome::Built,
          R"([{"formId":"0x1","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x2","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x3","hardPrereqs":[],"softPrereqs":[],"softNeeded":0},
              {"formId":"0x4","hardPrereqs":[],"softPrereqs":[],"softNeeded":0}])" },

        // ---- refused, not guessed ----
        { "a formId that is a number", R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":5}]}}})", Outcome::NotModelled, "" },
        { "a softNeeded that is a string",
          R"({"trustPrereqs":true,"schools":{"A":{"nodes":[{"formId":"0x1","softPrereqs":["0x2"],"softNeeded":"1"}]}}})",
          Outcome::NotModelled, "" },
    };

    int SelfTest()
    {
        int failed = 0;
        for (const auto& c : kCases) {
            const auto tree = OJson::parse(c.tree);
            const auto result = TreePrereqEntries::BuildPrerequisiteEntries(tree);
            bool ok = result.outcome == c.outcome;
            std::string got = result.outcome == Outcome::Built ? result.entries.dump() : std::string();
            if (ok && c.outcome == Outcome::Built) ok = got == OJson::parse(c.entries).dump();
            if (!ok) ++failed;
            std::printf("  [%s] %s: %s%s%s\n", ok ? "PASS" : "FAIL", c.name, TreePrereqEntries::OutcomeName(result.outcome),
                result.reason.empty() ? "" : " - ", result.reason.c_str());
            if (!ok && !got.empty()) std::printf("        got %s\n", got.c_str());
        }
        std::printf("self-test: %zu cases, %d passed, %d failed\n", std::size(kCases),
            static_cast<int>(std::size(kCases)) - failed, failed);
        return failed == 0 ? kExitOk : kExitMismatch;
    }

    // The enum's own name: the parity test reads it
    const char* OutcomeKey(Outcome outcome)
    {
        switch (outcome) {
            case Outcome::Built: return "Built";
            case Outcome::NoSchools: return "NoSchools";
            case Outcome::SlowPath: return "SlowPath";
            case Outcome::PanelThrows: return "PanelThrows";
            case Outcome::NotModelled: return "NotModelled";
        }
        return "?";
    }

    int DumpCases()
    {
        OJson cases = OJson::array();
        for (const auto& c : kCases) {
            cases.push_back({ { "name", c.name }, { "tree", std::string(c.tree) }, { "outcome", OutcomeKey(c.outcome) },
                { "entries", c.outcome == Outcome::Built ? OJson::parse(c.entries) : OJson() } });
        }
        std::printf("%s\n", cases.dump(2).c_str());
        return kExitOk;
    }

    int PrintFile(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: cannot be opened\n", path);
            return kExitUsage;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto tree = OJson::parse(text, nullptr, false);
        if (tree.is_discarded()) {
            std::fprintf(stderr, "%s: not JSON\n", path);
            return kExitUsage;
        }
        const auto result = TreePrereqEntries::BuildPrerequisiteEntries(tree);
        if (result.outcome != Outcome::Built) {
            std::printf("null\n");
            std::fprintf(stderr, "%s: %s - %s\n", path, TreePrereqEntries::OutcomeName(result.outcome), result.reason.c_str());
            return kExitNothingSent;
        }
        std::printf("%s\n", result.entries.dump().c_str());
        return kExitOk;
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") return SelfTest();
    if (argc == 2 && std::string_view(argv[1]) == "--dump-cases") return DumpCases();
    if (argc == 2) return PrintFile(argv[1]);
    std::fprintf(stderr, "usage: tree-prereq-check --self-test | --dump-cases | <tree.json>\n");
    return kExitUsage;
}
