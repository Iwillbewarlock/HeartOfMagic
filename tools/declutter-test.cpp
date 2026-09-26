// ============================================================================
// declutter-test  -  runs the native tree declutter pass outside the game
// ============================================================================
// The same LayoutDeclutter the plugin runs for the panel (DeclutterTree), on a
// tree saved before its declutter, so its positions can be compared with the
// panel's JavaScript (modules/layoutDeclutter.js) on the same tree.
//
// Usage:
//   declutter-test -i tree.json -o out.json [-r runs]
//
// tree.json: a saved tree (schools by name, globe, layoutMode, noRotate), or a
// DeclutterTree request. out.json: the reply the panel gets - positions as
// [formId, x, y] in the order the spells were taken, and the stats. With -r,
// the pass runs that many times and the fastest time is printed.
// ============================================================================

#include "Common.h"
#include "JsonFile.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "treebuilder/LayoutDeclutter.h"

namespace
{
    void PrintUsage()
    {
        std::cerr << "Usage: declutter-test -i tree.json -o out.json [-r runs]\n";
    }
}

int main(int argc, char** argv)
{
    std::string input;
    std::string output;
    int runs = 1;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            input = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output = argv[++i];
        } else if ((arg == "-r" || arg == "--runs") && i + 1 < argc) {
            runs = std::max(1, std::stoi(argv[++i]));
        } else {
            PrintUsage();
            return 1;
        }
    }
    if (input.empty()) {
        PrintUsage();
        return 1;
    }

    try {
        const nlohmann::json tree = ReadJsonFile(input);
        nlohmann::json reply;
        double fastest = 0;
        for (int run = 0; run < runs; run++) {
            reply = LayoutDeclutter::Run(tree);
            const double ms = reply.value("ms", 0.0);
            if (run == 0 || ms < fastest) fastest = ms;
        }
        std::cout << "spells " << reply["positions"].size() << ", moved " << reply.value("moved", 0)
                  << ", rounds " << reply.value("rounds", 0) << ", overlapsLeft " << reply.value("overlapsLeft", 0)
                  << ", linesLeft " << reply.value("linesLeft", -1) << ", linesMoved " << reply.value("linesMoved", 0)
                  << ", passes " << reply.value("passes", 0) << ", fastest of " << runs << ": " << fastest << " ms\n";
        if (!output.empty()) {
            std::ofstream file(output);
            file << reply.dump();
        }
    } catch (const std::exception& e) {
        std::cerr << "declutter-test: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
