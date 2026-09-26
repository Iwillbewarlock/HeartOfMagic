#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"

#include <algorithm>
#include <string_view>

// =============================================================================
// LibrarianTraits - the catalog's elements handed on to the tree and the card
//
// MergeCatalogElements is pure JSON in, JSON out (the offline harness runs the
// same merge). ClassifyScan is the one call a scanner makes after a scan.
// =============================================================================

namespace Librarian
{
    using namespace Librarian::Detail;

    namespace
    {
        constexpr std::string_view kElementPrefix = "element.";
        constexpr std::string_view kSoul = "soul";
        // Says "this hurts" and nothing more; the scanner leaves it off the
        // card once an element says so, and so does the merge.
        constexpr std::string_view kPlainDamageChip = "kind.damage";

        bool IsTreeElement(std::string_view element)
        {
            return std::find(std::begin(TREE_ELEMENTS), std::end(TREE_ELEMENTS), element) != std::end(TREE_ELEMENTS);
        }

        bool HasString(const json& list, std::string_view wanted)
        {
            return std::any_of(list.begin(), list.end(), [wanted](const json& item) {
                return item.is_string() && item.get_ref<const std::string&>() == wanted;
            });
        }

        bool IsConjured(const json& spell)
        {
            const auto traits = spell.find("traits");
            if (traits == spell.end() || !traits->is_array()) return false;
            return std::any_of(std::begin(CONJURED_KINDS), std::end(CONJURED_KINDS),
                [&traits](std::string_view kind) { return HasString(*traits, kind); });
        }

        // The element traits one spell should carry, as "element.<tag>" ids.
        std::vector<std::string> CatalogElementIds(const json& entry, bool conjured)
        {
            std::vector<std::string> ids;
            const auto elements = entry.find("elements");
            if (elements == entry.end() || !elements->is_array()) return ids;
            for (const auto& element : *elements) {
                if (!element.is_string()) continue;
                const auto& name = element.get_ref<const std::string&>();
                if (!IsTreeElement(name)) continue;
                if (conjured && name == kSoul) continue;
                ids.push_back(std::string(kElementPrefix) + name);
            }
            return ids;
        }

        // Rewrites one id list: the catalog's elements first - the ones it
        // already had keep their place in front, so the element a spell was
        // themed by stays its first - then everything else as it was. Returns
        // true when the list's elements changed. The card's list also sheds
        // plain damage once an element is there, and keeps to its length.
        bool ReplaceElements(json& list, const std::vector<std::string>& wanted, bool cardChips)
        {
            std::vector<std::string> elements;
            std::vector<json> rest;
            for (const auto& item : list) {
                if (item.is_string() && item.get_ref<const std::string&>().starts_with(kElementPrefix)) {
                    const auto& id = item.get_ref<const std::string&>();
                    if (std::find(wanted.begin(), wanted.end(), id) != wanted.end()) elements.push_back(id);
                } else {
                    rest.push_back(item);
                }
            }
            const std::size_t kept = elements.size();
            const std::size_t before = list.size() - rest.size();
            for (const auto& id : wanted) {
                if (std::find(elements.begin(), elements.end(), id) == elements.end()) elements.push_back(id);
            }
            if (elements.size() == kept && kept == before) return false;

            json rebuilt = json::array();
            for (const auto& id : elements) rebuilt.push_back(id);
            for (auto& item : rest) {
                if (cardChips && !elements.empty() && item.is_string() &&
                    item.get_ref<const std::string&>() == kPlainDamageChip) {
                    continue;
                }
                rebuilt.push_back(std::move(item));
            }
            if (cardChips && rebuilt.size() > MAX_CARD_CHIPS) {
                rebuilt.erase(rebuilt.begin() + static_cast<std::ptrdiff_t>(MAX_CARD_CHIPS), rebuilt.end());
            }
            list = std::move(rebuilt);
            return true;
        }
    }

    std::size_t MergeCatalogElements(json& scanDump, const json& catalog)
    {
        const auto spells = scanDump.find("spells");
        const auto entries = catalog.find("spells");
        if (spells == scanDump.end() || !spells->is_array() || entries == catalog.end() || !entries->is_object()) {
            return 0;
        }

        std::size_t changed = 0;
        for (auto& spell : *spells) {
            const std::string id = ReadField(spell, "persistentId");
            if (id.empty()) continue;
            const auto entry = entries->find(id);
            if (entry == entries->end()) continue;

            const auto wanted = CatalogElementIds(*entry, IsConjured(spell));
            bool touched = false;
            // Traits feed the tree builder and have no length limit; chips are
            // the card's keyword line and keep the scanner's.
            if (auto traits = spell.find("traits"); traits != spell.end() && traits->is_array()) {
                touched |= ReplaceElements(*traits, wanted, false);
            }
            if (auto chips = spell.find("chips"); chips != spell.end() && chips->is_array()) {
                touched |= ReplaceElements(*chips, wanted, true);
            }
            if (touched) ++changed;
        }
        return changed;
    }

    void ClassifyScan(std::string& scanJson)
    {
        try {
            json scanDump = json::parse(scanJson);

            // A scan with effects makes a fresh catalog; one without (a tome
            // list) takes its elements from the catalog the last full scan left.
            json catalog;
            if (!BuildAndWriteCatalog(scanDump, catalog) && !LoadCatalog(catalog)) {
                return;
            }

            const std::size_t changed = MergeCatalogElements(scanDump, catalog);
            if (changed == 0) return;

            scanJson = scanDump.dump();
            logger::info("Librarian: catalog elements merged into the traits of {} spells", changed);
        } catch (const std::exception& e) {
            logger::error("Librarian: merging catalog elements failed - {}", e.what());
        }
    }
}
