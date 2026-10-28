// ============================================================================
// declutter-test  -  runs the native tree declutter pass outside the game
// ============================================================================
// The same LayoutDeclutter the plugin runs for the panel (DeclutterTree), on a
// tree saved before its declutter, so its positions can be compared with the
// panel's JavaScript (modules/layoutDeclutter.js) on the same tree.
//
// Usage:
//   declutter-test -i tree.json -o out.json [-r runs]
//   declutter-test --self-test
//
// tree.json: a saved tree (schools by name, globe, layoutMode, noRotate), or a
// DeclutterTree request. out.json: the reply the panel gets - positions as
// [formId, x, y] in the order the spells were taken, and the stats. With -r,
// the pass runs that many times and the fastest time is printed.
//
// --self-test, no files needed: fixed spells (a graft's existing tree) come
// back exactly where they were, spread false leaves every spell unscaled, a
// free spell on a fixed one moves off it and its lines, and a request without
// those fields gives what the pass gave before them (positions and stats
// pinned). Exit 0 when every case passes, 1 on a failure.
// ============================================================================

#include "Common.h"
#include "JsonFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "treebuilder/LayoutDeclutter.h"
#include "treebuilder/LayoutDeclutterInternal.h"

namespace
{
    using Json = nlohmann::json;
    namespace DI = LayoutDeclutter::Internal;

    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;

    void PrintUsage()
    {
        std::cerr << "Usage: declutter-test -i tree.json -o out.json [-r runs]\n"
                     "       declutter-test --self-test\n";
    }

    struct Checker
    {
        int failed = 0;
        void Check(bool ok, const std::string& what)
        {
            if (!ok) ++failed;
            std::printf("    [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        }
    };

    Json Node(const std::string& formId, double x, double y, const Json& extra = Json::object())
    {
        Json n = { { "formId", formId }, { "x", x }, { "y", y } };
        for (const auto& [key, value] : extra.items()) n[key] = value;
        return n;
    }

    // One spell's place in the reply (found: false when it is not there)
    struct Spot
    {
        bool found = false;
        double x = 0;
        double y = 0;
    };

    Spot Find(const Json& reply, const std::string& formId)
    {
        Spot s;
        if (!reply.contains("positions") || !reply["positions"].is_array()) return s;
        for (const auto& p : reply["positions"]) {
            if (p.is_array() && p.size() == 3 && p[0] == formId) {
                s.found = true;
                s.x = p[1].get<double>();
                s.y = p[2].get<double>();
            }
        }
        return s;
    }

    bool At(const Spot& s, const Json& node)
    {
        return s.found && s.x == node["x"].get<double>() && s.y == node["y"].get<double>();
    }

    // Distance from p to the segment a-b, its ends included (no end margin)
    double SegmentDistance(const Spot& p, const Spot& a, const Spot& b)
    {
        const double vx = b.x - a.x, vy = b.y - a.y;
        const double l2 = vx * vx + vy * vy;
        const double t = l2 > 0 ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / l2, 0.0, 1.0) : 0.0;
        return std::hypot(a.x + vx * t - p.x, a.y + vy * t - p.y);
    }

    // Fixed spells at coordinates finer than the reply's 0.01, two on top of
    // each other, one inside the heart and one on a line between two others;
    // free spells on top of them and on their line
    Json FixedTree()
    {
        const Json fixed = { { "fixed", true } };
        const Json nodes = Json::array({
            Node("F1", 123.456789, -77.000123, { { "fixed", true }, { "children", { "F2", "N1" } } }),
            Node("F2", 323.456789, -77.000123, { { "fixed", true }, { "children", { "F4" } } }),
            Node("F3", 223.4, -77.01, fixed),  // on the F1-F2 line
            Node("F4", 323.456789, 122.987654, fixed),
            Node("F5", 323.456789, 122.987654, fixed),  // on top of F4
            Node("F6", 10.123456, -5.654321, fixed),     // inside the heart
            Node("R1", -150.111111, 40.222222, { { "isRoot", true }, { "fixed", false }, { "children", { "N2" } } }),
            Node("N1", 123.456789, -77.000123),  // on top of F1
            Node("N2", -150.111111, 40.222222),  // on top of R1
            Node("N3", 273.4, -77.0),            // on the F1-F2 line
            Node("N4", 12.0, -4.0),              // on F6, inside the heart
        });
        return { { "id", 1 }, { "globe", { { "x", 0 }, { "y", 0 }, { "radius", 45 } } }, { "spread", false },
            { "schools", Json::array({ { { "name", "Destruction" }, { "nodes", nodes } } }) } };
    }

    void TestFixedKeepExactly(Checker& check)
    {
        std::printf("  declutter: fixed nodes keep their x/y exactly\n");
        const Json base = FixedTree();
        const auto& given = base["schools"][0]["nodes"];
        const std::vector<std::pair<std::string, Json>> variants = {
            { "sectors on", Json::object() },
            { "noRotate", { { "noRotate", true } } },
            { "flat", { { "layoutMode", "flat" } } },
        };
        for (const auto& [label, extra] : variants) {
            Json request = base;
            for (const auto& [key, value] : extra.items()) request[key] = value;
            const Json reply = LayoutDeclutter::Run(request);
            std::string off, still;
            for (const auto& n : given) {
                const std::string id = n["formId"].get<std::string>();
                const bool fixed = n.value("fixed", false) || n.value("isRoot", false);
                const bool at = At(Find(reply, id), n);
                if (fixed && !at) off += " " + id;
                if (!fixed && at) still += " " + id;
            }
            check.Check(off.empty(), label + ": every fixed node (and the root) comes back bit for bit" + off);
            check.Check(still.empty(), label + ": the free spells on them moved" + still);
        }
    }

    void TestSpreadFalse(Checker& check)
    {
        std::printf("  declutter: spread false leaves fixed and free nodes unscaled\n");
        // Far apart, off the heart, no lines: nothing needs to move
        const Json nodes = Json::array({
            Node("A", 200.123456, 0.5, { { "fixed", true } }),
            Node("B", -300.25, 100.75),
            Node("C", 0.333333, 400.666666, { { "isRoot", true } }),
            Node("D", 500.5, -500.5),
        });
        Json request = { { "id", 2 }, { "spread", false },
            { "schools", Json::array({ { { "name", "Alteration" }, { "nodes", nodes } } }) } };
        const Json flat = LayoutDeclutter::Run(request);
        bool unscaled = true;
        for (const auto& n : nodes) unscaled = unscaled && At(Find(flat, n["formId"].get<std::string>()), n);
        check.Check(unscaled, "spread false: every spell, fixed or free, comes back where it was given");
        check.Check(flat.value("moved", -1) == 0, "spread false: moved is 0");

        request.erase("spread");
        const Json spread = LayoutDeclutter::Run(request);
        const Spot a = Find(spread, "A"), b = Find(spread, "B");
        check.Check(std::fabs(a.x - 200.123456 * DI::kSpread) < 0.01 && std::fabs(b.x - -300.25 * DI::kSpread) < 0.01 &&
                        std::fabs(b.y - 100.75 * DI::kSpread) < 0.01,
            "no spread field: fixed and free spells are spread x1.35 as before");
    }

    void TestFreeOffFixed(Checker& check)
    {
        std::printf("  declutter: a free node on top of a fixed one moves off it and off its links\n");
        // A grafted spell G under the existing B, laid on top of B; B's lines go to A, C and G
        const Json nodes = Json::array({
            Node("A", 300.0, 0.0, { { "fixed", true }, { "children", { "B" } } }),
            Node("B", 500.0, 0.0, { { "fixed", true }, { "children", { "C", "G" } } }),
            Node("C", 500.0, 200.0, { { "fixed", true } }),
            Node("G", 500.0, 0.0),
        });
        const Json request = { { "id", 3 }, { "spread", false },
            { "schools", Json::array({ { { "name", "Illusion" }, { "nodes", nodes } } }) } };
        const Json reply = LayoutDeclutter::Run(request);
        const Spot a = Find(reply, "A"), b = Find(reply, "B"), c = Find(reply, "C"), g = Find(reply, "G");
        check.Check(g.found && At(a, nodes[0]) && At(b, nodes[1]) && At(c, nodes[2]), "A, B and C did not move");
        const double fromB = std::hypot(g.x - b.x, g.y - b.y);
        check.Check(fromB >= 2 * DI::kNodeRadius, "G is off B (the circles apart): " + std::to_string(fromB));
        const double fromAB = SegmentDistance(g, a, b), fromBC = SegmentDistance(g, b, c);
        check.Check(fromAB >= DI::kNodeRadius && fromBC >= DI::kNodeRadius,
            "G is off B's lines to A and C: " + std::to_string(fromAB) + ", " + std::to_string(fromBC));
        check.Check(reply.value("overlapsLeft", -1) == 0 && reply.value("linesLeft", -1) == 0,
            "no overlap or line left");
    }

    // A tree with roots, lines, sectors, a spell in the heart and a crowd (push rounds run)
    Json RegressionTree()
    {
        Json alteration = Json::array();
        for (int i = 0; i < 20; i++) {
            char id[8];
            std::snprintf(id, sizeof id, "0x%02X", 0x21 + i);
            alteration.push_back(Node(id, (i % 5) * 3, 300 + (i / 5) * 3));
        }
        const Json destruction = Json::array({
            Node("0x01", 120, 0, { { "isRoot", true }, { "children", { "0x02", "0x03" } } }),
            Node("0x02", 200, 10, { { "children", { "0x04" } } }),
            Node("0x03", 205, -5, { { "children", { "0x05" } } }),
            Node("0x04", 300, 0),
            Node("0x05", 250, -2),
            Node("0x06", 30, 5),
        });
        const Json restoration = Json::array({
            Node("0x11", -120, 0, { { "isRoot", true }, { "children", { "0x12", "0x13" } } }),
            Node("0x12", -260, 40, { { "children", Json::array() } }),
            Node("0x13", -190, 20),
            Node("0x14", -195, 22),
        });
        return { { "id", 7 }, { "globe", { { "x", 0 }, { "y", 0 }, { "radius", 45 } } }, { "layoutMode", "" },
            { "noRotate", false },
            { "schools", Json::array({
                { { "name", "Destruction" }, { "startAngle", -60 }, { "endAngle", 60 }, { "nodes", destruction } },
                { { "name", "Alteration" }, { "startAngle", 60 }, { "endAngle", 120 }, { "nodes", alteration } },
                { { "name", "Restoration" }, { "startAngle", 120 }, { "endAngle", 240 }, { "nodes", restoration } },
            }) } };
    }

    // RegressionTree's reply from the pass before fixed and spread were read (3e9e2d66), ms left out
    const Json& RegressionReply()
    {
        static const Json expected = Json::parse(R"({"id":7,"lineCapped":false,"lineWork":101814,"linesLeft":0,
            "linesMoved":24,"moved":30,"overlapsLeft":0,"passes":5,"rounds":1,"positions":[["0x01",162.0,0.0],
            ["0x02",270.0,59.5],["0x03",276.75,-6.75],["0x04",405.0,0.0],["0x05",337.5,-2.7],["0x06",95.84,11.75],
            ["0x21",-82.84,415.05],["0x22",-66.96,364.0],["0x23",3.1,335.66],["0x24",85.15,358.23],
            ["0x25",48.2,492.43],["0x26",-33.0,485.57],["0x27",-4.61,514.9],["0x28",45.1,344.96],
            ["0x29",91.15,417.71],["0x2A",101.97,458.17],["0x2B",-46.68,403.1],["0x2C",-22.95,366.33],
            ["0x2D",8.1,477.1],["0x2E",62.58,389.76],["0x2F",62.97,450.1],["0x30",23.04,377.28],
            ["0x31",-42.72,444.15],["0x32",-1.28,439.68],["0x33",-5.83,401.98],["0x34",36.2,417.15],
            ["0x11",-162.0,0.0],["0x12",-324.0,100.77],["0x13",-296.34,4.0],["0x14",-263.25,29.7]]})");
        return expected;
    }

    bool SameAsBefore(Json reply)
    {
        reply.erase("ms");
        return reply == RegressionReply();
    }

    void TestNoFieldsUnchanged(Checker& check)
    {
        std::printf("  declutter: no fields given - result equal to before\n");
        const Json request = RegressionTree();
        check.Check(SameAsBefore(LayoutDeclutter::Run(request)), "positions and stats as captured before the change");

        Json inert = request;
        inert["spread"] = true;
        for (auto& school : inert["schools"]) {
            for (auto& n : school["nodes"]) n["fixed"] = false;
        }
        check.Check(SameAsBefore(LayoutDeclutter::Run(inert)), "spread true and fixed false on every node: the same");
    }

    int SelfTest()
    {
        Checker check;
        TestFixedKeepExactly(check);
        TestSpreadFalse(check);
        TestFreeOffFixed(check);
        TestNoFieldsUnchanged(check);
        std::printf("declutter-test --self-test: %s (%d failed)\n", check.failed ? "FAIL" : "PASS", check.failed);
        return check.failed ? kExitFailed : kExitOk;
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        try {
            return SelfTest();
        } catch (const std::exception& e) {
            std::cerr << "declutter-test --self-test: " << e.what() << "\n";
            return kExitFailed;
        }
    }
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
