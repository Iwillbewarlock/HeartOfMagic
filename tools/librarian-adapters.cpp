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

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>

#include "librarian/PerkAdapters.h"

using json = nlohmann::json;
namespace Adapters = Librarian::Adapters;

namespace
{
    json ReadDumpBody(const std::string& path)
    {
        // Same tolerance as the classifier's reader: skip console noise before
        // the first line that opens an object.
        std::ifstream file(path);
        if (!file.is_open()) throw std::runtime_error("Cannot open file: " + path);
        std::string line;
        std::string body;
        bool started = false;
        while (std::getline(file, line)) {
            if (!started) {
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] != '{') continue;
                started = true;
            }
            body += line;
            body += '\n';
        }
        if (!started) throw std::runtime_error("No JSON body found in: " + path);
        return json::parse(body);
    }
}

int RunAdapters(const std::string& dumpPath, const std::string& catalogPath,
    const std::string& adaptersDir, const std::string& reportPath)
{
    try {
        const json dump = ReadDumpBody(dumpPath);
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

        // Index the dump: spells by persistent id, users by effect key.
        std::vector<const json*> spells;
        std::unordered_map<std::string, std::vector<const json*>> users;
        for (const auto& spell : dump.at("spells")) {
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

        const Adapters::Plan plan = Adapters::BuildPlan(files, source);

        for (std::size_t f = 0; f < files.size(); ++f) {
            std::cout << "######## " << files[f].name << " - " << files[f].perkMod << "\n";
            for (std::size_t l = 0; l < files[f].lines.size(); ++l) {
                const auto& line = files[f].lines[l];
                const auto& s = plan.stats[f][l];
                std::cout << "  " << (line.enabled ? "" : "(off) ") << std::left << std::setw(36) << line.keyword
                          << " agree=" << std::setw(4) << s.agree << " writes=" << std::setw(4) << s.writes
                          << " gain=" << std::setw(4) << s.gain << " leaks=" << std::setw(4) << s.leaks
                          << " conditioned=" << s.conditioned << "\n";
            }
        }
        std::cout << "planned writes: " << plan.writes.size() << "\n";

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
