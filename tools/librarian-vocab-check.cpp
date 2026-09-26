// ============================================================================
// librarian-vocab-check  -  the --check-vocab half of librarian-test
// ============================================================================
// Kept apart from the scoring harness so neither file has to carry the other.

#include "Common.h"
#include "LibrarianVocabCheck.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "librarian/Librarian.h"
#include "librarian/TagVocabulary.h"

namespace
{
    // Pulls the entries of one `var NAME = [ 'a', 'b' ];` array out of the
    // JavaScript mirror. A real parser would be overkill for a flat string
    // list that exists only to be kept identical to the C++ one.
    std::vector<std::string> ReadJsArray(const std::string& source, const std::string& name)
    {
        std::vector<std::string> entries;

        const auto declaration = source.find("var " + name);
        if (declaration == std::string::npos) {
            return entries;
        }
        const auto open = source.find('[', declaration);
        const auto close = source.find(']', open);
        if (open == std::string::npos || close == std::string::npos) {
            return entries;
        }

        const std::string body = source.substr(open + 1, close - open - 1);
        std::size_t cursor = 0;
        while (true) {
            const auto quote = body.find('\'', cursor);
            if (quote == std::string::npos) {
                break;
            }
            const auto end = body.find('\'', quote + 1);
            if (end == std::string::npos) {
                break;
            }
            entries.push_back(body.substr(quote + 1, end - quote - 1));
            cursor = end + 1;
        }
        return entries;
    }

    bool CompareMirror(const char* label, const std::vector<std::string>& script,
        const std::vector<std::string>& native)
    {
        if (script.empty()) {
            std::cout << "  " << label << ": could not read the JavaScript array\n";
            return false;
        }
        if (script == native) {
            std::cout << "  " << label << ": " << native.size() << " tags, mirror matches\n";
            return true;
        }

        std::cout << "  " << label << ": MIRROR DRIFT (" << native.size()
            << " in C++, " << script.size() << " in JavaScript)\n";
        for (const auto& tag : native) {
            if (std::find(script.begin(), script.end(), tag) == script.end()) {
                std::cout << "    only in C++:        " << tag << "\n";
            }
        }
        for (const auto& tag : script) {
            if (std::find(native.begin(), native.end(), tag) == native.end()) {
                std::cout << "    only in JavaScript: " << tag << "\n";
            }
        }
        return false;
    }
}

// Rule files are checked by loading them: LibrarianRules drops any tag the
// vocabulary does not know and counts it, so a clean load is a clean file.
int CheckVocabulary(const std::string& rulesPath, const std::string& scriptPath)
{
    bool ok = true;

    const Librarian::RuleSet rules = Librarian::LoadRules(rulesPath);
    std::cout << "\nVOCABULARY CHECK\n";
    if (rules.rejectedTags > 0) {
        std::cout << "  rules: " << rules.rejectedTags
            << " tag(s) outside the vocabulary - see the warnings above\n";
        ok = false;
    } else {
        std::cout << "  rules: " << rules.rules.size()
            << " loaded, every tag is in the vocabulary\n";
    }

    if (!scriptPath.empty()) {
        std::ifstream file(scriptPath);
        if (!file.is_open()) {
            std::cout << "  mirror: cannot open " << scriptPath << "\n";
            ok = false;
        } else {
            const std::string source((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            const std::vector<std::string> nativeElements(
                std::begin(Librarian::ELEMENTS), std::end(Librarian::ELEMENTS));
            const std::vector<std::string> nativeTechniques(
                std::begin(Librarian::TECHNIQUES), std::end(Librarian::TECHNIQUES));

            ok &= CompareMirror("elements",
                ReadJsArray(source, "TAG_ELEMENTS"), nativeElements);
            ok &= CompareMirror("techniques",
                ReadJsArray(source, "TAG_TECHNIQUES"), nativeTechniques);
        }
    }

    std::cout << (ok ? "\nOK\n" : "\nFAILED\n");
    return ok ? 0 : 1;
}
