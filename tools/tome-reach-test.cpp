// =============================================================================
// tome-reach-test - which tree spells have tomes nothing hands out
// =============================================================================
//
//   tome-reach-test -i inputs.json -s spell_scan_output.json [-v: the tomes only a script property reaches]
//   tome-reach-test --self-test      the reader on made-up plugins (tome-reach-selftest.cpp)
//
// inputs.json: { "plugins": [ { "name": "...", "path": "..." } ... ] in load order,
//                "dataDirs": [ "..." ] }  (lab/tome-reach/make_inputs.py writes it
//                from an MO2 profile). Runs the plugin's own TomeReach pass, so its
//                answer is the plugin's; lab/tome-reach/expected-*.txt holds what
//                the Python measurement found on the same load order.

#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachPass.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

int RunSelfTest();

namespace
{
    json ReadJson(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot read " + path);
        return json::parse(in);
    }

    std::filesystem::path Utf8Path(const std::string& text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--self-test") return RunSelfTest();

    std::string inputs, scan;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "-v") verbose = true;
        else if (flag == "-i" && i + 1 < argc) inputs = argv[++i];
        else if (flag == "-s" && i + 1 < argc) scan = argv[++i];
    }
    if (inputs.empty() || scan.empty()) {
        std::fprintf(stderr, "usage: tome-reach-test -i inputs.json -s spell_scan_output.json\n");
        return 1;
    }

    try {
        const json in = ReadJson(inputs);
        std::vector<TomeReach::PluginFile> loadOrder;
        for (const auto& p : in["plugins"]) {
            loadOrder.push_back({ p["name"].get<std::string>(), Utf8Path(p["path"].get<std::string>()) });
        }
        std::vector<std::filesystem::path> configs;
        for (const auto& d : in["dataDirs"]) {
            for (auto& f : TomeReach::FindDistributionConfigs(Utf8Path(d.get<std::string>()))) configs.push_back(f);
        }

        const auto started = std::chrono::steady_clock::now();
        TomeReach::NameTable names;

        // The tomes: every BOOK that teaches a spell, its winning override
        std::vector<TomeReach::BookRecord> books;
        const std::unordered_set<TomeReach::Key> none;
        TomeReach::Evidence unused;
        for (const auto& plugin : loadOrder) TomeReach::ScanPlugin(plugin, names, none, unused, &books);
        std::unordered_map<TomeReach::Key, TomeReach::BookRecord> winning;
        for (auto& b : books) winning[b.book] = b;
        std::unordered_set<TomeReach::Key> tomes;
        std::unordered_map<TomeReach::Key, std::vector<TomeReach::Key>> tomesOf;
        for (const auto& [key, b] : winning) {
            if (!b.spell) continue;
            tomes.insert(key);
            tomesOf[b.spell].push_back(key);
        }

        const auto pass = TomeReach::FindReachedTomes(loadOrder, names, tomes, configs);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        std::printf("plugins %zu, unreadable %zu, damaged %zu, tomes %zu, reached by record %zu, only by a script "
                    "property or alias %zu, only by config %zu (%zu configs), kept for a damaged plugin %zu, %lld ms\n",
            loadOrder.size(), pass.unreadable.size(), pass.damaged.size(), tomes.size(), pass.byRecord,
            pass.byLooseOnly, pass.byConfig, pass.configFiles, pass.byDamaged, static_cast<long long>(ms));
        for (const auto& u : pass.unreadable) std::printf("  unreadable: %s\n", u.c_str());
        for (const auto& d : pass.damaged) std::printf("  damaged: %s\n", d.c_str());
        if (verbose) {
            // The tomes only a script property or quest alias reaches, and the plugin that named them
            std::printf("reached only by a script property or alias:\n");
            for (const auto& [key, from] : pass.looseFrom) {
                const auto b = winning.find(key);
                std::printf("  %s (%s) <- %s\n", b != winning.end() ? b->second.editorId.c_str() : "?",
                    names.Name(TomeReach::PluginOf(key)).c_str(), names.Name(from).c_str());
            }
        }

        // The tree's spells (taught by a tome, not in the voice slot) whose every tome is unreached
        std::map<std::string, std::vector<std::string>> lost;
        std::size_t checked = 0, total = 0;
        for (const auto& s : ReadJson(scan)["spells"]) {
            if (s.value("taughtByTome", false) != true || s.value("voiceSlot", false)) continue;
            const auto id = s.value("persistentId", std::string());
            const auto bar = id.find('|');
            if (bar == std::string::npos) continue;
            const auto key = TomeReach::MakeKey(names.Id(id.substr(0, bar)),
                static_cast<std::uint32_t>(std::stoul(id.substr(bar + 1), nullptr, 16)));
            ++checked;
            const auto it = tomesOf.find(key);
            if (it == tomesOf.end()) {
                lost["(no tome found)"].push_back(s.value("name", id));
                ++total;
                continue;
            }
            bool reached = false;
            for (const auto t : it->second) reached = reached || pass.reached.contains(t);
            if (!reached) {
                lost[s.value("plugin", std::string("?"))].push_back(s.value("name", id));
                ++total;
            }
        }
        std::printf("tree spells checked: %zu\nspells whose every tome nothing hands out: %zu\n", checked, total);
        for (const auto& [plugin, spells] : lost) {
            std::printf("%4zu  %s\n", spells.size(), plugin.c_str());
            for (const auto& n : spells) std::printf("        %s\n", n.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tome-reach-test: %s\n", e.what());
        return 1;
    }
    return 0;
}
