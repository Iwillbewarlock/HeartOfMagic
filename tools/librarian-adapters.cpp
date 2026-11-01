// ============================================================================
// librarian-test --adapters  -  perk adapter plan for a scan dump
// ============================================================================
// Runs the plugin's planner (BuildPlan) over a scan dump and its catalog, so
// the numbers here are the ones the game computes for the same load order -
// minus what a dump cannot show: items other than spells that share an effect
// (enchantments, scrolls, potions block a write in game) and spells the scan
// left out. Disabled lines are measured too and marked (off).
// ============================================================================

#include "Common.h"
#include "JsonFile.h"
#include "LibrarianAdapterRun.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include "librarian/PerkAdapters.h"

using json = nlohmann::json;
namespace Adapters = Librarian::Adapters;

int RunAdapters(const std::string& dumpPath, const std::string& catalogPath,
    const std::string& adaptersDir, const std::string& reportPath, bool verbose)
{
    try {
        const json dump = ReadDumpJson(dumpPath);
        const json catalogDocument = ReadJsonFile(catalogPath);
        const json& catalog = catalogDocument.at("spells");

        std::vector<Adapters::File> files;
        std::vector<std::string> problems;
        for (const auto& entry : std::filesystem::directory_iterator(adaptersDir)) {
            if (entry.path().extension() != ".json") continue;
            Adapters::File file;
            if (Adapters::ParseFile(ReadJsonFile(entry.path().string()), entry.path().filename().string(), file, problems)) {
                files.push_back(std::move(file));
            }
        }
        Adapters::SortFiles(files);
        for (const auto& problem : problems) std::cerr << "[adapter] " << problem << "\n";

        // Index the dump: spells by persistent id, users by effect key. Each id
        // once, as the game's catalog walk gives it (PlanSource): a dump with two
        // records under one id keeps its first, for the walk and the users alike.
        // A spell with no id (a runtime form in game) is kept every time.
        std::vector<const json*> spells;
        std::unordered_map<std::string, std::vector<const json*>> users;
        std::unordered_set<std::string> seen;
        std::size_t duplicates = 0;
        for (const auto& spell : dump.at("spells")) {
            const std::string id = spell.value("persistentId", std::string());
            if (!id.empty() && !seen.insert(id).second) {
                ++duplicates;
                continue;
            }
            spells.push_back(&spell);
            const auto effects = spell.find("effects");
            if (effects == spell.end()) continue;
            for (const auto& effect : *effects) {
                const std::string key = Adapters::EffectKey(effect);
                if (key.empty()) continue;
                auto& list = users[key];
                if (list.empty() || list.back() != &spell) list.push_back(&spell);
            }
        }
        if (duplicates > 0) {
            std::cerr << "[dump] " << duplicates << " spells repeat an earlier persistentId - planned on the first\n";
        }
        const auto idOf = [](const json& spell) { return spell.value("persistentId", std::string()); };
        const auto catalogEntry = [&](const std::string& id) -> const json* {
            const auto found = catalog.find(id);
            return found == catalog.end() ? nullptr : &*found;
        };

        Adapters::PlanSource source;
        source.measureDisabled = true;
        source.forEachCatalogSpell = [&](const auto& visit) {
            for (const json* spell : spells) {
                const std::string id = idOf(*spell);
                if (const json* entry = catalogEntry(id)) visit(id, *spell, *entry);
            }
        };
        source.usersOf = [&](const std::string& key) {
            std::vector<Adapters::EffectUser> result;
            const auto found = users.find(key);
            if (found == users.end()) return result;
            for (const json* spell : found->second) {
                const std::string id = idOf(*spell);
                result.push_back({ id, spell, catalogEntry(id) });
            }
            return result;
        };

        const auto planStarted = std::chrono::steady_clock::now();
        const Adapters::Plan plan = Adapters::BuildPlan(files, source);
        // On stderr, so the report on stdout compares byte for byte across builds
        std::cerr << "plan built in " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - planStarted).count() << " ms\n";

        for (std::size_t f = 0; f < files.size(); ++f) {
            std::cout << "######## " << files[f].name << " - " << files[f].perkMod << "\n";
            for (std::size_t l = 0; l < files[f].lines.size(); ++l) {
                const auto& line = files[f].lines[l];
                const auto& s = plan.stats[f][l];
                std::string label = !line.castingPerk.empty() ? "castingPerk " + line.castingPerk
                    : !line.formList.empty() ? line.formList : line.keyword;
                if (!line.damageElements.empty()) label += " (damage)";
                std::cout << "  " << (line.enabled ? "" : "(off) ") << std::left << std::setw(36) << label
                          << " agree=" << std::setw(4) << s.agree << " writes=" << std::setw(4) << s.writes
                          << " gain=" << std::setw(4) << s.gain << " leaks=" << std::setw(4) << s.leaks
                          << " conditioned=" << s.conditioned << "\n";
            }
        }
        if (plan.entriesWithoutSources > 0) {
            std::cout << "catalog entries without elementSources (taken as framework): "
                      << plan.entriesWithoutSources << "\n";
        }
        if (verbose) {
            std::cout << "\nWRITES (file | keyword | effect | spells using it)\n";
            for (const auto& write : plan.writes) {
                std::cout << "  " << files[write.file].name << " | " << write.keyword << " | " << write.effectKey << " |";
                const auto found = users.find(write.effectKey);
                if (found != users.end()) {
                    for (const json* spell : found->second) {
                        std::cout << " " << spell->value("name", std::string()) << " [" << idOf(*spell) << "]";
                    }
                }
                std::cout << "\n";
            }
        }
        std::cout << "planned writes: " << plan.writes.size() << "\n";
        std::cout << "planned casting perks: " << plan.castingPerkWrites.size() << "\n";

        if (!reportPath.empty()) {
            std::ofstream out(reportPath);
            out << std::setw(2) << Adapters::PlanReport(files, plan) << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
