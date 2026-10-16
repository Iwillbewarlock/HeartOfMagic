// =============================================================================
// blackflame-greybuild - command line (plugins/BlackFlame/README.md, "Grey duplicates")
// =============================================================================
//   blackflame-greybuild --map <map.json> --in <dir> [--in <dir> ...] --out <mod root>
//   blackflame-greybuild --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>]
//   blackflame-greybuild --forms <forms.json> --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>]
//   blackflame-greybuild --from-game [<request folder>] --out <mod root> [--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired]
//   blackflame-greybuild --write-empty-esls <folder>
//   blackflame-greybuild --dump <file.nif> [...]
//   blackflame-greybuild --roundtrip <file.nif> [...]
//   blackflame-greybuild --self-test
// --in folders hold loose files laid out Data-relative (meshes\...); they are searched in
// the order given and the first that has a file wins. --map builds what a fixed map lists
// (the shipped vanilla core); --request finds what one setup needs, builds it and writes
// the map (and, when needed, a small ESL of grey ADDN records) into --out. --forms adds the v2
// copies (GreyForms.h): full-grey copies of the listed forms' NIFs, the copies ESL and the copies
// map, on top of what --request writes (its ADDN list gives the grey indices). --from-game reads the request and the
// NIF dump the DLL wrote at kDataLoaded (GreyFromGame.cpp) and writes all of that into --out, warning and going on
// where a NIF cannot be read. --write-empty-esls writes the empty copies and local ESLs the mod ships.
// Exit 0 = success, 1 = a failure, 2 = bad arguments.

#include "GreyBuild.h"
#include "GreyForms.h"

#include <fmt/format.h>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFail = 1;
    constexpr int kExitUsage = 2;

    struct Options
    {
        std::filesystem::path map;
        std::filesystem::path request;
        std::filesystem::path forms;
        bool fromGame = false;
        std::filesystem::path fromGameDir;  // empty: found under Documents
        bool reclaimRetired = false;
        std::filesystem::path emptyEsls;
        std::vector<std::filesystem::path> inputs;
        std::filesystem::path out;
        std::vector<std::filesystem::path> files;  // --dump / --roundtrip
        float tint = GreyBuild::kDefaultTint;
        float tintAdd = GreyBuild::kDefaultTintAdd;  // v2i: additive palette shaders
        bool roundTrip = false;
        bool selfTest = false;
    };

    void PrintUsage()
    {
        std::cerr << "usage:\n"
                     "  blackflame-greybuild --map <map.json> --in <dir> [--in <dir> ...] --out <mod root>\n"
                     "  blackflame-greybuild --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>]\n"
                     "  blackflame-greybuild --forms <forms.json> --request <request.json> --in <dir> [...] --out <mod root> "
                     "[--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired]\n"
                     "  blackflame-greybuild --from-game [<request folder>] --out <output mod folder> [--tint <0..2>] [--tint-add <0..2>] "
                     "[--reclaim-retired]\n"
                     "      (the request folder defaults to <Documents>\\My Games\\Skyrim Special Edition[ GOG]\\SKSE\\"
                     "HeartOfMagic_BlackFlame; --out must exist and be the same folder every time; SE and AE, not VR)\n"
                     "  blackflame-greybuild --write-empty-esls <folder>\n"
                     "  blackflame-greybuild --dump <file.nif> [...]\n"
                     "  blackflame-greybuild --roundtrip <file.nif> [...]\n"
                     "  blackflame-greybuild --self-test\n";
    }

    [[nodiscard]] bool ParseTint(const char* a_text, float& a_tint)
    {
        try {
            std::size_t used = 0;
            a_tint = std::stof(a_text, &used);
            return a_text[used] == '\0' && a_tint >= GreyBuild::kMinTint && a_tint <= GreyBuild::kMaxTint;
        } catch (const std::logic_error&) {
            return false;
        }
    }

    [[nodiscard]] bool ParseArgs(int a_argc, char** a_argv, Options& a_options)
    {
        for (int i = 1; i < a_argc; ++i) {
            const std::string_view arg = a_argv[i];
            const bool hasValue = i + 1 < a_argc;
            if (arg == "--self-test") {
                a_options.selfTest = true;
            } else if (arg == "--from-game") {
                a_options.fromGame = true;
                if (hasValue && !GreyBuild::StartsWith(a_argv[i + 1], "--")) {
                    a_options.fromGameDir = a_argv[++i];
                }
            } else if (arg == "--reclaim-retired") {
                a_options.reclaimRetired = true;
            } else if (arg == "--write-empty-esls" && hasValue) {
                a_options.emptyEsls = a_argv[++i];
            } else if (arg == "--map" && hasValue) {
                a_options.map = a_argv[++i];
            } else if (arg == "--forms" && hasValue) {
                a_options.forms = a_argv[++i];
            } else if (arg == "--request" && hasValue) {
                a_options.request = a_argv[++i];
            } else if (arg == "--in" && hasValue) {
                a_options.inputs.emplace_back(a_argv[++i]);
            } else if (arg == "--out" && hasValue) {
                a_options.out = a_argv[++i];
            } else if (arg == "--tint" && hasValue) {
                if (!ParseTint(a_argv[++i], a_options.tint)) {
                    std::cerr << fmt::format("--tint '{}' must be a number in {}..{}\n", a_argv[i],
                        GreyBuild::kMinTint, GreyBuild::kMaxTint);
                    return false;
                }
            } else if (arg == "--tint-add" && hasValue) {
                if (!ParseTint(a_argv[++i], a_options.tintAdd)) {
                    std::cerr << fmt::format("--tint-add '{}' must be a number in {}..{}\n", a_argv[i],
                        GreyBuild::kMinTint, GreyBuild::kMaxTint);
                    return false;
                }
            } else if ((arg == "--dump" || arg == "--roundtrip") && hasValue) {
                a_options.roundTrip = arg == "--roundtrip";
                while (i + 1 < a_argc && !GreyBuild::StartsWith(a_argv[i + 1], "--")) {
                    a_options.files.emplace_back(a_argv[++i]);
                }
            } else {
                std::cerr << fmt::format("unknown or incomplete argument '{}'\n", arg);
                return false;
            }
        }
        return true;
    }

    /// The inputs exist and the output overlaps none of them (never write into a source folder).
    [[nodiscard]] bool CheckFolders(const Options& a_options)
    {
        for (const auto& dir : a_options.inputs) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) {
                std::cerr << fmt::format("--in '{}' is not a folder{}\n", dir.string(),
                    ec ? fmt::format(" ({})", ec.message()) : std::string());
                return false;
            }
            if (GreyBuild::IsInside(a_options.out, dir) || GreyBuild::IsInside(dir, a_options.out)) {
                std::cerr << fmt::format("--out '{}' and --in '{}' overlap: the builder never writes into a source "
                                         "folder. Pick an empty output folder.\n",
                    a_options.out.string(), dir.string());
                return false;
            }
        }
        return true;
    }

    void PrintTotals(const GreyBuild::Totals& a_totals)
    {
        std::cout << fmt::format("built {}, skipped {} (in no --in folder), failed {}\n", a_totals.built,
            a_totals.skipped, a_totals.failed);
        const auto& s = a_totals.shaders;
        std::cout << fmt::format("effect shaders {}: palette normal {} ({} without an alpha property), palette additive {}, "
                                 "left with a grey base colour {}\n", s.effectShaders, s.paletted, s.paletteNoAlpha, s.palettedAdd,
            s.paletteLeft);
    }

    [[nodiscard]] int RunMap(const Options& a_options)
    {
        const GreyBuild::GreyMap map = GreyBuild::LoadMap(a_options.map);
        std::cout << fmt::format("map {}: {} addon(s), {} model(s), tint {}\n", a_options.map.string(),
            map.addons.size(), map.models.size(), map.tint);
        const GreyBuild::Totals totals = GreyBuild::BuildFromMap(map, a_options.inputs, a_options.out, false);
        PrintTotals(totals);
        return totals.failed == 0 && totals.built > 0 ? kExitOk : kExitFail;
    }

    [[nodiscard]] int RunRequest(const Options& a_options)
    {
        const GreyBuild::Request request = GreyBuild::LoadRequest(a_options.request);
        std::cout << fmt::format("request {}: {} ADDN record(s), {} model(s)\n", a_options.request.string(),
            request.addons.size(), request.models.size());
        GreyBuild::Plan plan = GreyBuild::PlanFromRequest(request, a_options.inputs, a_options.tint);
        plan.map.tintAdd = a_options.tintAdd;
        for (const auto& line : plan.skipped) {
            std::cout << "SKIP  " << line << "\n";
        }
        for (const auto& [index, entry] : plan.map.addons) {
            std::cout << fmt::format("plan  ADDN {} ({}) -> {} as {}\n", index, entry.model, entry.grey,
                entry.greyForm);
        }
        if (!plan.problems.empty()) {
            for (const auto& line : plan.problems) {
                std::cout << "FAIL  " << line << "\n";
            }
            std::cout << "nothing written\n";
            return kExitFail;
        }
        const GreyBuild::Totals totals = GreyBuild::BuildFromMap(plan.map, a_options.inputs, a_options.out, true);
        PrintTotals(totals);
        if (totals.failed != 0 || totals.built == 0) {
            return kExitFail;
        }
        // The ESL before the map: a map is written only when the records it names are in place
        if (!GreyBuild::SyncLocalEsl(plan.newRecords, a_options.out / GreyBuild::kLocalPluginName)) {
            return kExitFail;
        }
        const auto mapFile = a_options.out / "SKSE" / "Plugins" / "HeartOfMagic_BlackFlame_greymap.json";
        GreyBuild::WriteMap(plan.map, mapFile,
            { "Generated by blackflame-greybuild --request for one setup (plugins/BlackFlame/README.md, \"Grey "
              "duplicates\").",
                "It replaces the shipped map when this output folder is a mod placed after Heart of Magic - Black "
                "Flame." });
        std::cout << "wrote " << mapFile.string() << "\n";
        return kExitOk;
    }

    [[nodiscard]] int RunForms(const Options& a_options)
    {
        const auto sources = GreyBuild::LoadFormsRequest(a_options.forms);
        const GreyBuild::Request addons = GreyBuild::LoadRequest(a_options.request);
        std::cout << fmt::format("forms {}: {} source form(s); request {}: {} ADDN record(s), {} model(s)\n",
            a_options.forms.string(), sources.size(), a_options.request.string(), addons.addons.size(),
            addons.models.size());
        GreyBuild::FormsRunOptions options;
        options.reclaimRetired = a_options.reclaimRetired;
        options.tintAdd = a_options.tintAdd;
        const auto totals = GreyBuild::BuildForms(sources, addons, a_options.inputs, a_options.out, a_options.tint, options);
        std::cout << fmt::format("forms: {} copies, {} copy NIFs, {} failed\n", totals.copies, totals.nifsBuilt,
            totals.failed);
        return totals.failed == 0 && totals.copies > 0 ? kExitOk : kExitFail;
    }
}

int main(int a_argc, char** a_argv)
{
    Options options;
    if (!ParseArgs(a_argc, a_argv, options)) {
        PrintUsage();
        return kExitUsage;
    }
    if (options.selfTest) {
        return GreyBuild::RunSelfTest();
    }
    if (!options.emptyEsls.empty()) {
        try {
            for (const char* name : { GreyBuild::kCopiesPluginName, GreyBuild::kLocalPluginName }) {
                GreyBuild::WriteEmptyEsl(options.emptyEsls / name);
                std::cout << "wrote " << GreyBuild::PathText(options.emptyEsls / name) << "\n";
            }
            return kExitOk;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return kExitFail;
        }
    }
    if (options.fromGame) {
        if (!options.inputs.empty() || !options.map.empty() || !options.request.empty() || !options.forms.empty()) {
            std::cerr << "--from-game reads only the game's request and NIF dump: leave out --in, --map, --request and --forms\n";
            return kExitUsage;
        }
        try {
            return GreyBuild::RunFromGame(options.fromGameDir, options.out, options.tint, options.reclaimRetired, options.tintAdd);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return kExitFail;
        }
    }
    if (!options.files.empty()) {
        int result = kExitOk;
        for (const auto& file : options.files) {
            const int one = options.roundTrip ? GreyBuild::RoundTripReport(file) : GreyBuild::DumpNif(file);
            result = one == kExitOk ? result : kExitFail;
        }
        return result;
    }
    // --map alone, --request alone, or --forms with --request
    const bool oneSource = options.map.empty() != options.request.empty() && (options.forms.empty() || options.map.empty());
    if (!oneSource || options.inputs.empty() || options.out.empty()) {
        PrintUsage();
        return kExitUsage;
    }
    try {
        if (!CheckFolders(options)) {
            return kExitUsage;
        }
        if (!options.forms.empty()) {
            return RunForms(options);
        }
        return options.map.empty() ? RunRequest(options) : RunMap(options);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return kExitFail;
    }
}
