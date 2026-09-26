#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"

#include <chrono>
#include <sstream>

// =============================================================================
// LibrarianCatalog - the four axes, and reading and writing spell_catalog.json
//
// Everything above WriteCatalogFile is pure JSON in, JSON out, so the offline
// harness produces byte-identical catalogs to the ones the game writes.
// =============================================================================

namespace Librarian
{
    using namespace Librarian::Detail;

    namespace
    {
        constexpr const char* CATALOG_FILE = "spell_catalog.json";

        std::string CatalogPath()
        {
            return DataPath(CATALOG_FILE).string();
        }

        std::string UtcTimestamp()
        {
            const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm utc{};
            gmtime_s(&utc, &now);

            std::ostringstream stream;
            stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        double ConfidenceForSource(const std::string& source)
        {
            if (source == SOURCE_MGEF) {
                return CONFIDENCE_MGEF;
            }
            if (source == SOURCE_FRAMEWORK) {
                return CONFIDENCE_FRAMEWORK;
            }
            if (source == SOURCE_MANUAL) {
                return CONFIDENCE_MANUAL;
            }
            return CONFIDENCE_NONE;
        }

        // A spell's confidence is that of its weakest evidence: an entry whose
        // techniques rest on a framework keyword is only as portable as that
        // framework, whatever its elements came from.
        double EntryConfidence(const TagSet& tags)
        {
            const bool hasElements = !tags.elements.empty();
            const bool hasTechniques = !tags.techniques.empty();

            if (!hasElements && !hasTechniques) {
                return CONFIDENCE_NONE;
            }
            if (!hasTechniques) {
                return ConfidenceForSource(tags.elementSource);
            }
            if (!hasElements) {
                return ConfidenceForSource(tags.techniqueSource);
            }
            return std::min(ConfidenceForSource(tags.elementSource),
                ConfidenceForSource(tags.techniqueSource));
        }

        // Any effect with a non-zero area makes the spell an area spell.
        bool HasAreaEffect(const json& spell)
        {
            const auto effects = spell.find("effects");
            if (effects == spell.end() || !effects->is_array()) {
                return false;
            }

            for (const auto& effect : *effects) {
                const auto area = effect.find("area");
                if (area != effect.end() && area->is_number() && area->get<double>() > 0.0) {
                    return true;
                }
            }
            return false;
        }

        bool AnyEffectHasArchetype(const json& spell, std::initializer_list<const char*> wanted)
        {
            const auto effects = spell.find("effects");
            if (effects == spell.end() || !effects->is_array()) {
                return false;
            }

            for (const auto& effect : *effects) {
                if (!effect.is_object()) {
                    continue;
                }
                const std::string archetype = ReadField(effect, "archetype");
                for (const char* name : wanted) {
                    if (archetype == name) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Summons, the reanimated and hazards put something in the world that
        // then acts on actors, so they target an actor as well as wherever they
        // were placed.
        //
        // Two further guesses were tried against the answer set and both cost
        // accuracy, so neither is here: adding actor for cloaks (a cloak burns
        // whoever comes close) and adding aoe for cloaks and hazards (a hazard
        // covers ground, and its radius sits on the hazard record rather than
        // on the effect). Together they took targeting from 66.3% to 61.5% -
        // they fixed about a dozen spells and broke more, because Spell
        // Research does not stack self with aoe the way that implies.
        bool ReachesAnActor(const json& spell)
        {
            return AnyEffectHasArchetype(spell,
                { "SummonCreature", "SpawnHazard", "Reanimate" });
        }
    }

    // =========================================================================
    // AXES
    // =========================================================================

    SpellAxes DeriveAxes(const json& spell)
    {
        SpellAxes axes;
        if (!spell.is_object()) {
            return axes;
        }

        // school and tier are the scanner's own words, only cased differently
        const std::string school = ReadField(spell, "school");
        if (!school.empty() && school != "Unknown") {
            axes.school = Lowered(school);
        }

        const std::string tier = ReadField(spell, "skillLevel");
        if (!tier.empty() && tier != "Unknown") {
            axes.tier = Lowered(tier);
        }

        // "Fire and Forget" is one word to every consumer of this catalog
        const std::string casting = ReadField(spell, "castingType");
        if (casting == "Fire and Forget") {
            axes.casting = "fireforget";
        } else if (casting == "Concentration") {
            axes.casting = "concentration";
        }

        // Delivery says where the spell lands. Aimed reads as an actor, not a
        // location - that was measured against Firebolt, Flames, Fear and Ice
        // Spike, and an early guess of "location" was wrong.
        const std::string delivery = ReadField(spell, "delivery");
        if (delivery == "Self") {
            axes.targeting.insert("self");
        } else if (delivery == "Aimed" || delivery == "Touch" || delivery == "Target Actor") {
            axes.targeting.insert("actor");
        } else if (delivery == "Target Location") {
            axes.targeting.insert("location");
        }

        if (HasAreaEffect(spell)) {
            axes.targeting.insert("aoe");
        }
        if (ReachesAnActor(spell)) {
            axes.targeting.insert("actor");
        }

        return axes;
    }

    // =========================================================================
    // BUILDING
    // =========================================================================

    json BuildCatalog(const json& scanDump, const RuleSet& rules, CatalogStats& stats)
    {
        stats = CatalogStats{};

        json catalog;
        catalog["version"] = CATALOG_VERSION;
        catalog["vocab"] = CATALOG_VOCAB;
        catalog["generated"] = UtcTimestamp();
        catalog["spells"] = json::object();

        const auto spells = scanDump.find("spells");
        if (spells == scanDump.end() || !spells->is_array()) {
            logger::error("Librarian: scan dump has no spells array - catalog is empty");
            return catalog;
        }

        for (const auto& spell : *spells) {
            if (!spell.is_object()) {
                continue;
            }

            const std::string persistentId = ReadField(spell, "persistentId");
            if (persistentId.empty()) {
                ++stats.skipped;
                continue;
            }

            const TagSet tags = Classify(spell, rules);
            const SpellAxes axes = DeriveAxes(spell);

            json entry;
            entry["school"] = axes.school;
            entry["tier"] = axes.tier;
            entry["casting"] = axes.casting;
            entry["targeting"] = axes.targeting;
            entry["elements"] = tags.elements;
            entry["techniques"] = tags.techniques;
            entry["source"] = { { "elements", tags.elementSource },
                { "techniques", tags.techniqueSource } };
            entry["confidence"] = EntryConfidence(tags);

            catalog["spells"][persistentId] = entry;
            ++stats.spells;
            if (!tags.Empty()) {
                ++stats.tagged;
            }
        }

        return catalog;
    }

    // =========================================================================
    // FILE
    // =========================================================================

    bool LoadCatalog(json& catalog)
    {
        const auto path = DataPath(CATALOG_FILE);
        if (!ReadJsonFile(path, catalog)) {
            logger::info("Librarian: no readable catalog at '{}'", path.string());
            return false;
        }
        return true;
    }

    std::string BuildAndWriteCatalog(const std::string& scanJson)
    {
        // A failure anywhere in here is logged and swallowed: the scan itself
        // has already succeeded by the time this runs, and losing the dump
        // because the librarian tripped would be the worse outcome.
        try {
            const json scanDump = json::parse(scanJson);
            const RuleSet rules = LoadRules(RulesPath().string());
            if (rules.rules.empty()) {
                logger::warn("Librarian: no rules loaded - skipping catalog");
                return "";
            }

            // A scan without effects (its field settings left them out) has
            // nothing to classify: every spell would come out untagged. Keep
            // the catalog there is rather than overwrite it with that - seen
            // once, all 1,440 spells written without an element.
            bool anyEffects = false;
            const auto spellList = scanDump.find("spells");
            if (spellList != scanDump.end() && spellList->is_array()) {
                for (const auto& spell : *spellList) {
                    const auto effects = spell.find("effects");
                    if (effects != spell.end() && effects->is_array() && !effects->empty()) {
                        anyEffects = true;
                        break;
                    }
                }
            }
            if (!anyEffects) {
                logger::warn("Librarian: the scan has no effects to classify - catalog left as it is");
                return "";
            }

            CatalogStats stats;
            const json catalog = BuildCatalog(scanDump, rules, stats);

            std::filesystem::create_directories(DATA_DIR);
            const std::string path = CatalogPath();

            std::ofstream file(path);
            if (!file.is_open()) {
                logger::error("Librarian: cannot open '{}' for writing", path);
                return "";
            }
            file << catalog.dump(2);
            file.close();

            logger::info("Librarian: wrote {} ({} spells, {} tagged, {} without a persistentId)",
                path, stats.spells, stats.tagged, stats.skipped);
            return path;
        } catch (const std::exception& e) {
            logger::error("Librarian: catalog build failed - {}", e.what());
            return "";
        }
    }
}
