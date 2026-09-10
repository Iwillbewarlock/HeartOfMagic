// ============================================================================
// librarian-test  -  Offline harness for the tag librarian
// ============================================================================
// Classifies a scan dump with the shipped rule files and, when an answer set
// is supplied, scores the result against it. Runs the same LibrarianClassify
// the plugin runs, so the numbers it prints are the plugin's numbers.
//
// Usage:
//   librarian-test -i spell_scan_output.json -r SKSE/Plugins/SpellLearning/librarian
//   librarian-test -i dump.json -r rules -a spellresearch_archetypes_1160.json
//   librarian-test -i dump.json -r rules -o catalog.json
//
// The answer set is the Spell Research archetype export: an array of
// { name, formId, esp, elements[], techniques[] }. Joining it to the dump is
// exact match on (plugin lowercased, formId & 0xFFFFFF) and nothing else -
// falling back to the low 12 bits for ESL plugins mismatched every time.
// ============================================================================

#include "Common.h"
#include "JsonFile.h"
#include "LibrarianScore.h"
#include "LibrarianVocabCheck.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>

#include <nlohmann/json.hpp>

#include "librarian/Librarian.h"

using json = nlohmann::json;

namespace
{
    constexpr std::uint32_t LOCAL_FORM_ID_MASK = 0xFFFFFF;

    // ========================================================================
    // Input
    // ========================================================================

    void PrintUsage(const char* argv0)
    {
        std::cerr
            << "Usage: " << argv0 << " [options]\n"
            << "\n"
            << "Required:\n"
            << "  -i, --input   <file>  Scan dump JSON (spell_scan_output.json)\n"
            << "  -r, --rules   <dir>   Directory of librarian rule files\n"
            << "\n"
            << "Optional:\n"
            << "  -a, --answers <file>  Answer set to score against\n"
            << "  -o, --output  <file>  Write the tagged catalog here\n"
            << "  -v, --verbose         List per spell misses for the answer set\n"
            << "  -t, --tier    <name>  Use only rules of this evidence tier,\n"
            << "                        e.g. \"mgef\" to measure without frameworks\n"
            << "  -h, --help            Show this help\n"
            << "\n"
            << "Score a catalog instead of a dump (no rules needed):\n"
            << "  --catalog <spell_catalog.json> -a <answers>\n"
            << "      Scores tags and the four axes straight out of a catalog,\n"
            << "      which is how a catalog the game wrote gets checked against\n"
            << "      the numbers the offline run produces.\n"
            << "\n"
            << "Vocabulary check (no dump needed):\n"
            << "  --check-vocab -r <dir> [-j <tagVocabulary.js>]\n"
            << "      Fails when a rule file uses a tag outside TagVocabulary.h,\n"
            << "      or when the JavaScript mirror has drifted from it.\n";
    }

    // A scan dump can carry console noise ahead of the JSON when it was saved
    // through the panel, so start reading at the first line that opens an
    // object rather than at byte zero.
    json ReadDump(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + path);
        }

        std::string line;
        std::string body;
        bool started = false;
        while (std::getline(file, line)) {
            if (!started) {
                const auto firstReal = line.find_first_not_of(" \t\r");
                if (firstReal == std::string::npos || line[firstReal] != '{') {
                    continue;
                }
                started = true;
            }
            body += line;
            body += '\n';
        }

        if (!started) {
            throw std::runtime_error("No JSON body found in: " + path);
        }
        return json::parse(body);
    }

}

int main(int argc, char* argv[])
{
    spdlog::set_pattern("[%l] %v");

    std::string inputPath;
    std::string rulesPath;
    std::string answersPath;
    std::string outputPath;
    std::string scriptPath;
    std::string tier;
    std::string catalogPath;
    bool verbose = false;
    bool checkVocabulary = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasNext = (i + 1) < argc;

        if ((arg == "-i" || arg == "--input") && hasNext) {
            inputPath = argv[++i];
        } else if ((arg == "-r" || arg == "--rules") && hasNext) {
            rulesPath = argv[++i];
        } else if ((arg == "-a" || arg == "--answers") && hasNext) {
            answersPath = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && hasNext) {
            outputPath = argv[++i];
        } else if ((arg == "-j" || arg == "--script") && hasNext) {
            scriptPath = argv[++i];
        } else if ((arg == "-t" || arg == "--tier") && hasNext) {
            tier = argv[++i];
        } else if (arg == "--catalog" && hasNext) {
            catalogPath = argv[++i];
        } else if (arg == "--check-vocab") {
            checkVocabulary = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (checkVocabulary) {
        if (rulesPath.empty()) {
            PrintUsage(argv[0]);
            return 1;
        }
        try {
            return CheckVocabulary(rulesPath, scriptPath);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }

    if (!catalogPath.empty()) {
        if (answersPath.empty()) {
            std::cerr << "--catalog needs -a <answers> to score against\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
        try {
            const auto entries = IndexCatalog(ReadJsonFile(catalogPath));
            if (entries.empty()) {
                std::cerr << "Catalog has no spells\n";
                return 1;
            }
            std::cout << "\nCATALOG  (" << entries.size() << " spells)\n";

            const json answers = ReadJsonFile(answersPath);
            ReportScore(answers, TagsFromCatalog(entries), verbose);
            ReportAxes(answers, entries);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (inputPath.empty() || rulesPath.empty()) {
        PrintUsage(argv[0]);
        return 1;
    }

    try {
        const json dump = ReadDump(inputPath);

        // A dump taken after the keyword patch ran describes a load order the
        // plugin itself edited, so its coverage is not what another user sees.
        const auto patched = dump.find("keywordPatchApplied");
        if (patched != dump.end() && patched->is_boolean() && patched->get<bool>()) {
            std::cout << "\nWARNING: this dump was taken after the vanilla keyword patch ran"
                << " - coverage here is higher than an unpatched load order would give\n";
        }

        Librarian::RuleSet rules = Librarian::LoadRules(rulesPath);
        if (!tier.empty()) {
            const std::size_t before = rules.rules.size();
            std::erase_if(rules.rules,
                [&tier](const Librarian::Rule& rule) { return rule.source != tier; });
            std::cout << "\nTIER FILTER '" << tier << "': kept " << rules.rules.size()
                << " of " << before << " rules\n";
        }
        if (rules.rules.empty()) {
            std::cerr << "No rules loaded from " << rulesPath << "\n";
            return 1;
        }
        std::cout << "\n";

        // Build the catalog the plugin would build, then report on that. The
        // numbers below therefore describe what actually ships, and the -o file
        // is the same object rather than a second rendering of it.
        Librarian::CatalogStats stats;
        const json catalog = Librarian::BuildCatalog(dump, rules, stats);
        if (stats.skipped > 0) {
            std::cout << stats.skipped << " scan entries had no persistentId and were skipped\n";
        }

        const auto entries = IndexCatalog(catalog);
        ReportCoverage(entries);

        if (!answersPath.empty()) {
            const json answers = ReadJsonFile(answersPath);
            ReportScore(answers, TagsFromCatalog(entries), verbose);
            ReportAxes(answers, entries);
        }

        if (!outputPath.empty()) {
            std::ofstream file(outputPath);
            if (!file.is_open()) {
                throw std::runtime_error("Cannot write: " + outputPath);
            }
            file << catalog.dump(2) << "\n";
            std::cout << "\nWrote catalog to " << outputPath << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
