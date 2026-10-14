// tree-save-check - offline check of SaveSpellTree's no-spells guard
// (plugins/spelllearning/include/uimanager/TreeSaveCheck.h).
//
//   tree-save-check --self-test        made-up trees, no files needed
//   tree-save-check <tree.json>...     each file: the text scan against a full
//                                      parse (spells under schools.*.nodes)
//
// Exit 0 when every case agrees, 1 on a disagreement, 2 on a bad argument or a
// file that cannot be read.

#include "uimanager/TreeSaveCheck.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitMismatch = 1;
    constexpr int kExitUsage = 2;

    // What the guard used to do: parse the tree and count every school's nodes
    std::size_t CountByParse(const std::string& text)
    {
        const auto tree = nlohmann::json::parse(text, nullptr, false);
        std::size_t count = 0;
        if (!tree.is_object() || !tree.contains("schools") || !tree["schools"].is_object()) return count;
        for (const auto& [name, school] : tree["schools"].items()) {
            if (school.is_object() && school.contains("nodes") && school["nodes"].is_array()) count += school["nodes"].size();
        }
        return count;
    }

    struct Case
    {
        const char* name;
        std::string_view text;
        bool expected;
    };

    int SelfTest()
    {
        static constexpr Case kCases[] = {
            { "one school with a spell", R"({"schools":{"Destruction":{"nodes":[{"formId":"0x1"}]}}})", true },
            { "pretty-printed, CRLF and tabs", "{\r\n\t\"schools\": {\r\n\t\t\"Alteration\": {\r\n\t\t\t\"nodes\" :\r\n\t\t\t[\r\n\t\t\t\t{ \"formId\": \"0x2\" }\r\n\t\t\t]\r\n\t\t}\r\n\t}\r\n}", true },
            { "the spell in the last school", R"({"schools":{"A":{"nodes":[]},"B":{"nodes":[]},"C":{"nodes":[{"formId":"0x3"}]}}})", true },
            { "every school empty", R"({"schools":{"A":{"nodes":[]},"B":{"nodes":[ ]}}})", false },
            { "no schools", R"({"version":2,"schools":{}})", false },
            { "empty text", "", false },
            { "not JSON", "not a tree", false },
            { "cut off after the bracket", R"({"schools":{"A":{"nodes":[)", false },
            { "\"nodes\" as a value", R"({"schools":{"A":{"name":"nodes","list":[{"x":1}]}}})", false },
            { "\"nodes\" inside a spell name", R"({"schools":{"A":{"nodes":[],"root":"he said \"nodes\": [{\"x\":1}]"}}})", false },
            { "a backslash before the quote of a key", R"({"schools":{"A":{"name":"\\","nodes":[{"formId":"0x4"}]}}})", true },
            { "nodes not an array", R"({"schools":{"A":{"nodes":{"formId":"0x5"}}}})", false },
        };
        int failed = 0;
        for (const auto& c : kCases) {
            const bool got = TreeSaveCheck::HasSpellNodes(c.text);
            const bool ok = got == c.expected;
            if (!ok) ++failed;
            std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", c.name, got ? "has spells" : "no spells");
        }
        std::printf("self-test: %d of %zu failed\n", failed, std::size(kCases));
        return failed == 0 ? kExitOk : kExitMismatch;
    }

    int CheckFile(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: cannot be opened\n", path);
            return kExitUsage;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto started = std::chrono::steady_clock::now();
        const bool scan = TreeSaveCheck::HasSpellNodes(text);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        const auto parsed = CountByParse(text);
        const bool ok = scan == (parsed > 0);
        std::printf("  [%s] %s: %zu bytes, scan says %s in %lld us, parse counts %zu spells\n", ok ? "PASS" : "FAIL", path,
            text.size(), scan ? "spells" : "no spells", static_cast<long long>(us), parsed);
        return ok ? kExitOk : kExitMismatch;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: tree-save-check --self-test | tree-save-check <tree.json>...\n");
        return kExitUsage;
    }
    if (std::string_view(argv[1]) == "--self-test") return SelfTest();
    int result = kExitOk;
    for (int i = 1; i < argc; ++i) {
        const int one = CheckFile(argv[i]);
        if (one > result) result = one;
    }
    return result;
}
