// ============================================================================
// treebuilder-test  —  Standalone TreeBuilder test harness
// ============================================================================
// Runs the Classic TreeBuilder outside of Skyrim/SKSE (the one builder left;
// Tree, Graph, Thematic and Oracle were removed on 2026-09-27).
//
// Usage:
//   treebuilder-test -i spells.json -o tree.json -t classic
//   treebuilder-test --input spells.json --output tree.json --type classic --seed 42
//   treebuilder-test -i spells.json -o tree.json -t classic -c config.json
//
// Input JSON: either a raw array of spell objects, or an object with a
// "spells" key (matches the in-game UIManager format).
// ============================================================================

#include "Common.h"
#include "JsonFile.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "treebuilder/TreeBuilder.h"

using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================

static void PrintUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Required:\n"
        << "  -i, --input  <file>   Input spell JSON file\n"
        << "  -o, --output <file>   Output tree JSON file\n"
        << "  -t, --type   <type>   Builder type: classic\n"
        << "\n"
        << "Optional:\n"
        << "  -s, --seed   <n>      Random seed (default: 0)\n"
        << "  -c, --config <file>   Config JSON file (default: built-in defaults)\n"
        << "  -h, --help            Show this help\n";
}

static void WriteJsonFile(const std::string& path, const json& data)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + path);
    }
    file << data.dump(2);
}

// ============================================================================
// Structure check
// ============================================================================
// A tree the builder hands back must link by formId strings only, and must
// branch: a school of kChainCheckMinNodes or more spells where no spell has two
// children is a chain, which is what a broken link pass leaves behind after the
// reachability repair rebuilds it (all_valid still says true then). A config
// that allows one child per spell (max_children_per_node 1) asks for a chain, so
// it is not flagged then. Prints per school: nodes, links, spells with 2+
// children, most children, deepest depth.

static constexpr int kChainCheckMinNodes = 10;

static bool CheckStructure(const json& treeData, int maxChildrenPerNode)
{
    const bool chainAllowed = maxChildrenPerNode <= 1;
    if (!treeData.contains("schools") || !treeData["schools"].is_object()) {
        std::cerr << "Structure: no schools\n";
        return false;
    }
    bool ok = true;
    for (const auto& [school, sd] : treeData["schools"].items()) {
        if (!sd.contains("nodes") || !sd["nodes"].is_array()) continue;
        const auto& nodes = sd["nodes"];
        std::unordered_map<std::string, const json*> byId;
        for (const auto& n : nodes) {
            if (n.is_object()) byId[n.value("formId", std::string(""))] = &n;
        }

        int links = 0, badLinks = 0, branching = 0;
        size_t mostChildren = 0;
        for (const auto& n : nodes) {
            if (!n.is_object()) continue;
            for (const char* key : { "children", "prerequisites" }) {
                if (!n.contains(key) || !n[key].is_array()) continue;
                for (const auto& id : n[key]) {
                    if (!id.is_string()) ++badLinks;
                }
            }
            if (n.contains("children") && n["children"].is_array()) {
                const auto count = n["children"].size();
                links += static_cast<int>(count);
                if (count >= 2) ++branching;
                mostChildren = (std::max)(mostChildren, count);
            }
        }

        // Deepest depth: breadth first from the root along children
        int deepest = 0;
        const auto root = sd.value("root", std::string(""));
        if (byId.count(root)) {
            std::unordered_map<std::string, int> depth{ { root, 0 } };
            std::queue<std::string> todo;
            todo.push(root);
            while (!todo.empty()) {
                const auto id = todo.front();
                todo.pop();
                const json* n = byId[id];
                if (!n->contains("children") || !(*n)["children"].is_array()) continue;
                for (const auto& c : (*n)["children"]) {
                    if (!c.is_string()) continue;
                    const auto cid = c.get<std::string>();
                    if (!byId.count(cid) || depth.count(cid)) continue;
                    depth[cid] = depth[id] + 1;
                    deepest = (std::max)(deepest, depth[cid]);
                    todo.push(cid);
                }
            }
        }

        const bool chain = !chainAllowed && static_cast<int>(nodes.size()) >= kChainCheckMinNodes && mostChildren <= 1;
        std::cout << "Structure " << school << ": nodes=" << nodes.size() << " links=" << links
                  << " branching=" << branching << " maxChildren=" << mostChildren
                  << " maxDepth=" << deepest << " nonStringLinks=" << badLinks
                  << (chain ? "  CHAIN" : "") << "\n";
        if (badLinks > 0 || chain) ok = false;
    }
    return ok;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[])
{
    std::string inputPath;
    std::string outputPath;
    std::string type;
    std::string configPath;
    int         seed = 0;

    // ----- Parse arguments ---------------------------------------------------
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            inputPath = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputPath = argv[++i];
        } else if ((arg == "-t" || arg == "--type") && i + 1 < argc) {
            type = argv[++i];
        } else if ((arg == "-s" || arg == "--seed") && i + 1 < argc) {
            seed = std::stoi(argv[++i]);
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (inputPath.empty() || outputPath.empty() || type.empty()) {
        std::cerr << "Error: --input, --output, and --type are required.\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    // ----- Map type to command string ----------------------------------------
    static const std::unordered_map<std::string, std::string> kTypeToCommand = {
        {"classic",  "build_tree_classic"},
    };

    auto it = kTypeToCommand.find(type);
    if (it == kTypeToCommand.end()) {
        std::cerr << "Error: unknown type '" << type
                  << "'. The one builder is: classic\n";
        return 1;
    }
    auto command = it->second;

    // ----- Set up spdlog console logger --------------------------------------
    spdlog::set_level(spdlog::level::info);

    // ----- Read input --------------------------------------------------------
    json inputJson;
    try {
        inputJson = ReadJsonFile(inputPath);
    } catch (const std::exception& e) {
        std::cerr << "Error reading input: " << e.what() << "\n";
        return 1;
    }

    // Accept either a raw array or an object with a "spells" key
    std::vector<json> spells;
    json spellsArray;

    if (inputJson.is_array()) {
        spellsArray = inputJson;
    } else if (inputJson.is_object() && inputJson.contains("spells")) {
        spellsArray = inputJson["spells"];
    } else {
        std::cerr << "Error: input JSON must be an array of spells, "
                     "or an object with a \"spells\" key.\n";
        return 1;
    }

    spells.reserve(spellsArray.size());
    for (auto& s : spellsArray) {
        spells.push_back(std::move(s));
    }

    // ----- Read config -------------------------------------------------------
    json configJson = json::object();
    if (!configPath.empty()) {
        try {
            configJson = ReadJsonFile(configPath);
        } catch (const std::exception& e) {
            std::cerr << "Error reading config: " << e.what() << "\n";
            return 1;
        }
    }

    // Inject seed from CLI (overrides config file if both provided)
    if (seed != 0 || !configJson.contains("seed")) {
        configJson["seed"] = seed;
    }

    // ----- Build tree --------------------------------------------------------
    std::cout << "Building " << type << " tree from "
              << spells.size() << " spells (seed=" << seed << ")...\n";

    auto result = TreeBuilder::Build(command, spells, configJson);

    if (!result.success) {
        std::cerr << "Build failed: " << result.error << "\n";
        return 1;
    }

    // ----- Write output ------------------------------------------------------
    try {
        WriteJsonFile(outputPath, result.treeData);
    } catch (const std::exception& e) {
        std::cerr << "Error writing output: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Done. " << spells.size() << " spells -> " << outputPath
              << " (" << result.elapsedMs << " ms)\n";

    // The same default the builder reads the config with
    const int maxChildrenPerNode = TreeBuilder::BuildConfig::FromJson(configJson).maxChildrenPerNode;
    if (!CheckStructure(result.treeData, maxChildrenPerNode)) {
        std::cerr << "Structure check FAILED: a link that is not a formId, or a school that does not branch\n";
        return 2;
    }
    return 0;
}
