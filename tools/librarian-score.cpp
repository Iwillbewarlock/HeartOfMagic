// ============================================================================
// librarian-score  -  the reporting half of librarian-test
// ============================================================================

#include "Common.h"
#include "LibrarianScore.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

// Local id of a form: the low three bytes, the plugin index stripped off.
static constexpr std::uint32_t LOCAL_FORM_ID_MASK = 0xFFFFFF;

using json = nlohmann::json;

namespace
{
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

}

// ========================================================================
// Reporting
// ========================================================================

// Reads the catalog rather than classifying again: the catalog is what
// ships, so the coverage reported here is the coverage that ships.
void ReportCoverage(const std::map<JoinKey, json>& entries)
{
    std::size_t noTags = 0;
    std::size_t elementsOnly = 0;
    std::size_t techniquesOnly = 0;
    std::size_t both = 0;
    std::map<std::string, std::size_t> elementCounts;
    std::map<std::string, std::size_t> techniqueCounts;

    for (const auto& [key, entry] : entries) {
        const auto elements = ReadTagSet(entry, "elements");
        const auto techniques = ReadTagSet(entry, "techniques");

        if (elements.empty() && techniques.empty()) {
            ++noTags;
        } else if (techniques.empty()) {
            ++elementsOnly;
        } else if (elements.empty()) {
            ++techniquesOnly;
        } else {
            ++both;
        }

        for (const auto& tag : elements) {
            ++elementCounts[tag];
        }
        for (const auto& tag : techniques) {
            ++techniqueCounts[tag];
        }
    }

    const auto total = entries.size();
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

// Writes the tags ReportCoverage already computed; nothing is classified
// a second time.
// ========================================================================
// Catalog mode
// ========================================================================

// Indexes a catalog by the same join key the dump uses, so a catalog the
// game wrote can be scored through exactly the code path a dump takes.
std::map<JoinKey, json> IndexCatalog(const json& catalog)
{
    std::map<JoinKey, json> entries;

    const auto spells = catalog.find("spells");
    if (spells == catalog.end() || !spells->is_object()) {
        return entries;
    }

    for (const auto& [persistentId, entry] : spells->items()) {
        const auto bar = persistentId.find('|');
        if (bar == std::string::npos) {
            continue;
        }
        entries[JoinKey{ Lowered(persistentId.substr(0, bar)),
            ParseLocalId(persistentId.substr(bar + 1)) }] = entry;
    }
    return entries;
}

std::map<JoinKey, Librarian::TagSet> TagsFromCatalog(const std::map<JoinKey, json>& entries)
{
    std::map<JoinKey, Librarian::TagSet> tagged;

    for (const auto& [key, entry] : entries) {
        Librarian::TagSet tags;
        tags.elements = ReadTagSet(entry, "elements");
        tags.techniques = ReadTagSet(entry, "techniques");
        tagged[key] = tags;
    }
    return tagged;
}

// The four axes come out of scan fields with no rules involved, so they are
// scored separately: a wrong axis is a bug in the derivation, not a rule
// that needs tuning.
void ReportAxes(const json& answers, const std::map<JoinKey, json>& entries)
{
    struct AxisCount { std::size_t matched = 0; std::size_t total = 0; };
    AxisCount school, tier, casting, targeting;

    const auto scoreOne = [](const json& entry, const json& answer,
        const char* ours, const char* theirs, AxisCount& count) {
        const auto mine = entry.find(ours);
        const auto want = answer.find(theirs);
        if (want == answer.end() || !want->is_string()) {
            return;
        }
        ++count.total;
        if (mine != entry.end() && mine->is_string()
            && mine->get<std::string>() == want->get<std::string>()) {
            ++count.matched;
        }
    };

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

        const auto found = entries.find(JoinKey{ Lowered(esp->get<std::string>()),
            ParseLocalId(formId->get<std::string>()) });
        if (found == entries.end()) {
            continue;
        }

        scoreOne(found->second, answer, "school", "school", school);
        scoreOne(found->second, answer, "tier", "tier", tier);
        scoreOne(found->second, answer, "casting", "casting", casting);

        ++targeting.total;
        if (ReadTagSet(found->second, "targeting") == ReadTagSet(answer, "targets")) {
            ++targeting.matched;
        }
    }

    const auto line = [](const char* label, const AxisCount& count) {
        const double pct = count.total
            ? static_cast<double>(count.matched) * 100.0 / static_cast<double>(count.total) : 0.0;
        std::cout << "  " << std::left << std::setw(12) << label << std::right
            << std::fixed << std::setprecision(1)
            << std::setw(6) << pct << "%   " << count.matched << "/" << count.total << "\n";
    };

    std::cout << "\nAXES vs answer set  (derived from scan fields, no rules)\n";
    line("school", school);
    line("tier", tier);
    line("casting", casting);
    line("targeting", targeting);
}

