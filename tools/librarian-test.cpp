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

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <unordered_map>

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
            << "  -h, --help            Show this help\n";
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

    json ReadJsonFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        return json::parse(file);
    }

    // ========================================================================
    // Join key
    // ========================================================================

    struct JoinKey
    {
        std::string plugin;
        std::uint32_t localId = 0;

        bool operator<(const JoinKey& other) const
        {
            if (plugin != other.plugin) {
                return plugin < other.plugin;
            }
            return localId < other.localId;
        }
    };

    std::string Lowered(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::uint32_t ParseLocalId(const std::string& formId)
    {
        try {
            return static_cast<std::uint32_t>(std::stoul(formId, nullptr, 16)) & LOCAL_FORM_ID_MASK;
        } catch (const std::exception&) {
            return 0;
        }
    }

    // persistentId is "Plugin.esp|0x00123456"; fall back to the separate
    // plugin and formId fields when a narrower preset left it out.
    bool MakeScanKey(const json& spell, JoinKey& key)
    {
        std::string plugin;
        std::string formId;

        const auto persistent = spell.find("persistentId");
        if (persistent != spell.end() && persistent->is_string()) {
            const std::string text = persistent->get<std::string>();
            const auto bar = text.find('|');
            if (bar != std::string::npos) {
                plugin = text.substr(0, bar);
                formId = text.substr(bar + 1);
            }
        }

        if (plugin.empty()) {
            const auto pluginField = spell.find("plugin");
            if (pluginField != spell.end() && pluginField->is_string()) {
                plugin = pluginField->get<std::string>();
            }
        }
        if (formId.empty()) {
            const auto formField = spell.find("formId");
            if (formField != spell.end() && formField->is_string()) {
                formId = formField->get<std::string>();
            }
        }

        if (plugin.empty() || formId.empty()) {
            return false;
        }

        key.plugin = Lowered(plugin);
        key.localId = ParseLocalId(formId);
        return true;
    }

    // ========================================================================
    // Scoring
    // ========================================================================

    struct AxisScore
    {
        std::size_t truePositives = 0;
        std::size_t predicted = 0;
        std::size_t actual = 0;
        std::size_t exactMatches = 0;

        [[nodiscard]] double Precision() const
        {
            return predicted ? static_cast<double>(truePositives) / static_cast<double>(predicted) : 0.0;
        }
        [[nodiscard]] double Recall() const
        {
            return actual ? static_cast<double>(truePositives) / static_cast<double>(actual) : 0.0;
        }
        [[nodiscard]] double F1() const
        {
            const double p = Precision();
            const double r = Recall();
            return (p + r) > 0.0 ? 2.0 * p * r / (p + r) : 0.0;
        }
    };

    std::set<std::string> ReadTagSet(const json& object, const char* key)
    {
        std::set<std::string> tags;
        const auto found = object.find(key);
        if (found == object.end() || !found->is_array()) {
            return tags;
        }
        for (const auto& entry : *found) {
            if (entry.is_string()) {
                tags.insert(entry.get<std::string>());
            }
        }
        return tags;
    }

    // Per tag error counts. Which tag is wrong matters more than the totals:
    // it says which rule to fix first.
    struct TagError
    {
        std::size_t truePositives = 0;
        std::size_t falsePositives = 0;
        std::size_t falseNegatives = 0;

        [[nodiscard]] std::size_t Errors() const { return falsePositives + falseNegatives; }
    };

    using TagErrors = std::map<std::string, TagError>;

    void Accumulate(const std::set<std::string>& predicted,
        const std::set<std::string>& actual, AxisScore& score, TagErrors& errors)
    {
        std::size_t hits = 0;
        for (const auto& tag : predicted) {
            if (actual.count(tag)) {
                ++hits;
                ++errors[tag].truePositives;
            } else {
                ++errors[tag].falsePositives;
            }
        }
        for (const auto& tag : actual) {
            if (!predicted.count(tag)) {
                ++errors[tag].falseNegatives;
            }
        }

        score.truePositives += hits;
        score.predicted += predicted.size();
        score.actual += actual.size();
        if (predicted == actual) {
            ++score.exactMatches;
        }
    }

    void PrintTagErrors(const char* label, const TagErrors& errors)
    {
        std::vector<std::pair<std::string, TagError>> sorted(errors.begin(), errors.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second.Errors() > b.second.Errors(); });

        std::cout << "\n  " << label << " - worst tags (hit / false alarm / missed)\n";
        for (const auto& [tag, error] : sorted) {
            if (error.Errors() == 0) {
                continue;
            }
            std::cout << "    " << std::left << std::setw(14) << tag << std::right
                << std::setw(4) << error.truePositives
                << std::setw(6) << error.falsePositives
                << std::setw(6) << error.falseNegatives << "\n";
        }
    }

    void PrintAxis(const char* label, const AxisScore& score, std::size_t pairs)
    {
        std::cout << "  " << std::left << std::setw(12) << label
            << std::right << std::fixed << std::setprecision(1)
            << "  P " << std::setw(5) << score.Precision() * 100.0
            << "  R " << std::setw(5) << score.Recall() * 100.0
            << "  F1 " << std::setw(5) << score.F1() * 100.0
            << "   exact " << score.exactMatches << "/" << pairs
            << "\n";
    }

    // ========================================================================
    // Reporting
    // ========================================================================

    void ReportCoverage(const json& spells, const Librarian::RuleSet& rules,
        std::map<JoinKey, Librarian::TagSet>& tagged)
    {
        std::size_t noTags = 0;
        std::size_t elementsOnly = 0;
        std::size_t techniquesOnly = 0;
        std::size_t both = 0;
        std::map<std::string, std::size_t> elementCounts;
        std::map<std::string, std::size_t> techniqueCounts;

        for (const auto& spell : spells) {
            const Librarian::TagSet tags = Librarian::Classify(spell, rules);

            JoinKey key;
            if (MakeScanKey(spell, key)) {
                tagged[key] = tags;
            }

            if (tags.Empty()) {
                ++noTags;
            } else if (tags.techniques.empty()) {
                ++elementsOnly;
            } else if (tags.elements.empty()) {
                ++techniquesOnly;
            } else {
                ++both;
            }

            for (const auto& tag : tags.elements) {
                ++elementCounts[tag];
            }
            for (const auto& tag : tags.techniques) {
                ++techniqueCounts[tag];
            }
        }

        const auto total = spells.size();
        const auto percent = [total](std::size_t n) {
            return total ? static_cast<double>(n) * 100.0 / static_cast<double>(total) : 0.0;
        };

        std::cout << "COVERAGE  (" << total << " spells)\n"
            << std::fixed << std::setprecision(1)
            << "  elements + techniques  " << std::setw(5) << both << "  " << percent(both) << "%\n"
            << "  elements only          " << std::setw(5) << elementsOnly << "  " << percent(elementsOnly) << "%\n"
            << "  techniques only        " << std::setw(5) << techniquesOnly << "  " << percent(techniquesOnly) << "%\n"
            << "  no tags at all         " << std::setw(5) << noTags << "  " << percent(noTags)
            << "%   <- what the fallback has to cover\n\n";

        std::cout << "TAGS ASSIGNED\n  elements  ";
        for (const auto& [tag, count] : elementCounts) {
            std::cout << tag << "(" << count << ") ";
        }
        std::cout << "\n  techniques  ";
        for (const auto& [tag, count] : techniqueCounts) {
            std::cout << tag << "(" << count << ") ";
        }
        std::cout << "\n\n";
    }

    void ReportScore(const json& answers, const std::map<JoinKey, Librarian::TagSet>& tagged,
        bool verbose)
    {
        AxisScore elements;
        AxisScore techniques;
        TagErrors elementErrors;
        TagErrors techniqueErrors;
        std::size_t pairs = 0;
        std::size_t unmatched = 0;
        std::map<std::string, std::size_t> unmatchedByPlugin;

        for (const auto& answer : answers) {
            if (!answer.is_object()) {
                continue;
            }

            const auto esp = answer.find("esp");
            const auto formId = answer.find("formId");
            if (esp == answer.end() || formId == answer.end()
                || !esp->is_string() || !formId->is_string()) {
                continue;
            }

            JoinKey key{ Lowered(esp->get<std::string>()),
                ParseLocalId(formId->get<std::string>()) };

            const auto found = tagged.find(key);
            if (found == tagged.end()) {
                ++unmatched;
                ++unmatchedByPlugin[key.plugin];
                continue;
            }

            ++pairs;
            const auto actualElements = ReadTagSet(answer, "elements");
            const auto actualTechniques = ReadTagSet(answer, "techniques");
            Accumulate(found->second.elements, actualElements, elements, elementErrors);
            Accumulate(found->second.techniques, actualTechniques, techniques, techniqueErrors);

            if (verbose && (found->second.elements != actualElements
                || found->second.techniques != actualTechniques)) {
                std::cout << "  MISS " << key.plugin << "|0x" << std::hex << key.localId << std::dec
                    << "  got {";
                for (const auto& tag : found->second.elements) std::cout << tag << " ";
                for (const auto& tag : found->second.techniques) std::cout << "+" << tag << " ";
                std::cout << "}  want {";
                for (const auto& tag : actualElements) std::cout << tag << " ";
                for (const auto& tag : actualTechniques) std::cout << "+" << tag << " ";
                std::cout << "}\n";
            }
        }

        if (verbose) {
            std::cout << "\n";
        }

        std::cout << "SCORE vs answer set  (" << pairs << " joined, "
            << unmatched << " unmatched)\n";
        PrintAxis("elements", elements, pairs);
        PrintAxis("techniques", techniques, pairs);

        AxisScore combined;
        combined.truePositives = elements.truePositives + techniques.truePositives;
        combined.predicted = elements.predicted + techniques.predicted;
        combined.actual = elements.actual + techniques.actual;
        PrintAxis("both", combined, pairs);

        PrintTagErrors("elements", elementErrors);
        PrintTagErrors("techniques", techniqueErrors);

        std::cout << "\n  unmatched by plugin: ";
        for (const auto& [plugin, count] : unmatchedByPlugin) {
            std::cout << plugin << "(" << count << ") ";
        }
        std::cout << "\n";
    }

    void WriteCatalog(const std::string& path, const json& spells,
        const Librarian::RuleSet& rules)
    {
        json catalog;
        catalog["version"] = 1;
        catalog["vocab"] = "tags-draft";
        catalog["spells"] = json::object();

        for (const auto& spell : spells) {
            const auto persistent = spell.find("persistentId");
            if (persistent == spell.end() || !persistent->is_string()) {
                continue;
            }

            const Librarian::TagSet tags = Librarian::Classify(spell, rules);

            json entry;
            entry["elements"] = tags.elements;
            entry["techniques"] = tags.techniques;
            entry["source"] = { { "elements", tags.elementSource },
                { "techniques", tags.techniqueSource } };
            catalog["spells"][persistent->get<std::string>()] = entry;
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot write: " + path);
        }
        file << catalog.dump(2) << "\n";
        std::cout << "\nWrote catalog to " << path << "\n";
    }
}

int main(int argc, char* argv[])
{
    spdlog::set_pattern("[%l] %v");

    std::string inputPath;
    std::string rulesPath;
    std::string answersPath;
    std::string outputPath;
    bool verbose = false;

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

    if (inputPath.empty() || rulesPath.empty()) {
        PrintUsage(argv[0]);
        return 1;
    }

    try {
        const json dump = ReadDump(inputPath);
        const auto spellsField = dump.find("spells");
        if (spellsField == dump.end() || !spellsField->is_array()) {
            std::cerr << "Dump has no spells array\n";
            return 1;
        }
        const json& spells = *spellsField;

        const Librarian::RuleSet rules = Librarian::LoadRules(rulesPath);
        if (rules.rules.empty()) {
            std::cerr << "No rules loaded from " << rulesPath << "\n";
            return 1;
        }
        std::cout << "\n";

        std::map<JoinKey, Librarian::TagSet> tagged;
        ReportCoverage(spells, rules, tagged);

        if (!answersPath.empty()) {
            ReportScore(ReadJsonFile(answersPath), tagged, verbose);
        }

        if (!outputPath.empty()) {
            WriteCatalog(outputPath, spells, rules);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
