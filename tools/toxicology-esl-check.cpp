// toxicology-esl-check - the Korean ESL still matches the English one (toxicology-esl-compare.h, ESL.md "Korean ESL")
//
//   toxicology-esl-check <english.esp> <korean.esp>
//
// Prints "PASS ..." or one "FAIL ..." line per problem. Exit 0 on a match, 1 on a mismatch, 2 on a bad argument
// or a file that cannot be read. DistributionToxicology.cmake runs it before the Toxicology zips are made.

#include "toxicology-esl-compare.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;

    std::optional<std::vector<std::uint8_t>> ReadFile(const char* a_path)
    {
        std::ifstream file(a_path, std::ios::binary);
        if (!file) return std::nullopt;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (file.bad()) return std::nullopt;
        return bytes;
    }
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: toxicology-esl-check <english.esp> <korean.esp>\n");
        return kExitUsage;
    }
    const auto english = ReadFile(argv[1]);
    const auto korean = ReadFile(argv[2]);
    for (const auto& [bytes, path] : { std::pair{ &english, argv[1] }, std::pair{ &korean, argv[2] } }) {
        if (!*bytes) {
            std::fprintf(stderr, "FAIL toxicology-esl-check: cannot read %s; check the path\n", path);
            return kExitUsage;
        }
    }

    const auto problems = ToxicologyEsl::CompareToxicologyEsl(*english, *korean);
    if (problems.empty()) {
        std::printf("PASS toxicology-esl-check: %s matches %s outside the five text subrecords\n", argv[2], argv[1]);
        return kExitOk;
    }
    for (const auto& problem : problems) std::printf("FAIL toxicology-esl-check: %s\n", problem.c_str());
    std::printf("The Korean ESL no longer matches the English one: make it again from the English file (plugins/Toxicology/ESL.md, \"Korean ESL\").\n");
    return kExitFail;
}
