// ============================================================================
// librarian-test --spellresearch  -  Spell Research registrations for a dump
// ============================================================================
// Runs the plugin's planner over the SR rules file alone (its formListsNone
// marks live in that one plan), turns the plan into registrations the way the
// game does, and counts them per SR list. With -a the SR elements and
// techniques go through the catalog scorer, so the numbers line up with the
// librarian's own score against the same answer set.
// ============================================================================

#include "Common.h"
#include "JsonFile.h"
#include "LibrarianScore.h"
#include "LibrarianSpellResearchRun.h"

#include <iomanip>
#include <iostream>
#include <map>
#include <unordered_map>

#include "librarian/PerkAdapters.h"
#include "librarian/SpellResearch.h"

using json = nlohmann::json;
namespace Adapters = Librarian::Adapters;
namespace SR = Librarian::SpellResearch;

int RunSpellResearch(const std::string& dumpPath, const std::string& catalogPath,
    const std::string& rulesFile, const std::string& answersPath, const std::string& reportPath, bool verbose)
{
    try {
        const json dump = ReadDumpJson(dumpPath);
        const json catalogDocument = ReadJsonFile(catalogPath);
        const json& catalog = catalogDocument.at("spells");
        const json rules = ReadJsonFile(rulesFile);

        std::vector<std::string> problems;
        SR::Lists lists;
        const bool listsOk = SR::ParseLists(rules, lists, problems);
        std::vector<Adapters::File> files(1);
        const bool fileOk = Adapters::ParseFile(rules, "spellresearch.json", files[0], problems);
        for (const auto& problem : problems) std::cerr << "[spellresearch] " << problem << "\n";
        if (!listsOk || !fileOk) return 1;

        std::unordered_map<std::string, const json*> spells;
        for (const auto& spell : dump.at("spells")) {
            spells.emplace(spell.value("persistentId", std::string()), &spell);
        }

        // List lines only: no leak check, so no effect users are needed
        Adapters::PlanSource source;
        source.forEachCatalogSpell = [&](const auto& visit) {
            for (const auto& [id, spell] : spells) {
                const auto entry = catalog.find(id);
                if (entry != catalog.end()) visit(id, *spell, *entry);
            }
        };
        source.usersOf = [](const std::string&) { return std::vector<Adapters::EffectUser>{}; };
        const Adapters::Plan plan = Adapters::BuildPlan(files, source);

        // The catalog holds every spell the scan tagged; the dump decides what
        // exists and, as the game does, which spells a tome teaches. A dump
        // without the mark (older than taughtByTome) keeps every spell.
        json present = json::object();
        std::size_t notByTome = 0;
        bool marked = false;
        for (const auto& [id, entry] : catalog.items()) {
            const auto spell = spells.find(id);
            if (spell == spells.end()) continue;
            const auto taught = spell->second->find("taughtByTome");
            if (taught != spell->second->end() && taught->is_boolean()) {
                marked = true;
                if (!taught->get<bool>()) {
                    ++notByTome;
                    continue;
                }
            }
            present[id] = entry;
        }
        if (!marked) std::cout << "\nNOTE: the dump has no taughtByTome marks - every catalog spell counts, NPC and quest spells too\n";
        const SR::Registrations registrations = SR::Build(lists, plan, present, [&](const std::string& id) {
            const auto found = spells.find(id);
            return found == spells.end() ? std::string() : found->second->value("tomePersistentId", std::string());
        });

        std::map<std::string, std::size_t> perList;
        std::size_t withTome = 0;
        json scored = json::object();
        for (const auto& registration : registrations.spells) {
            json elements = json::array();
            json techniques = json::array();
            for (const auto& list : registration.lists) {
                const std::string archetype = SR::ArchetypeOf(lists, list);
                ++perList[archetype.empty() ? list : archetype];
                if (archetype.starts_with("element.")) elements.push_back(archetype.substr(8));
                if (archetype.starts_with("technique.")) techniques.push_back(archetype.substr(10));
            }
            if (!registration.tome.empty()) ++withTome;
            scored[registration.spellId] = { { "elements", elements }, { "techniques", techniques } };
        }

        std::cout << "\nSPELL RESEARCH  (" << registrations.spells.size() << " spells registered, "
                  << registrations.noSchoolOrTier << " without an SR school or tier, " << notByTome
                  << " no tome teaches, " << withTome << " with a tome)\n";
        for (const auto& [name, count] : perList) {
            std::cout << "  " << std::left << std::setw(28) << name << count << "\n";
        }
        std::cout << "planned registrations: " << registrations.spells.size() << "\n";

        if (!answersPath.empty()) {
            const json answers = ReadJsonFile(answersPath);
            std::cout << "\nSPELL RESEARCH LABELS\n";
            ReportScore(answers, TagsFromCatalog(IndexCatalog(json{ { "spells", scored } })), verbose);
        }

        if (!reportPath.empty()) {
            std::ofstream out(reportPath);
            out << std::setw(2) << json{ { "perList", perList }, { "registered", registrations.spells.size() },
                { "noSchoolOrTier", registrations.noSchoolOrTier }, { "withTome", withTome },
                { "lines", Adapters::PlanReport(files, plan) } } << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
