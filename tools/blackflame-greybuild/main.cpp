// =============================================================================
// blackflame-greybuild - command line (plugins/BlackFlame/README.md, "Grey duplicates")
// =============================================================================
//   blackflame-greybuild --map <map.json> --in <dir> [--in <dir> ...] --out <mod root>
//   blackflame-greybuild --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>]
//   blackflame-greybuild --forms <forms.json> --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired] [--no-textures]
//   blackflame-greybuild --from-game [<request folder>] --out <mod root> [--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired] [--no-textures] [--retire-left-out]
//   blackflame-greybuild --from-game [<request folder>] --staging <dir> [--job-memory <MiB>] [--commit-reserve <MiB>] [--parent-pid <pid>]
//   blackflame-greybuild --write-empty-esls <folder>
//   blackflame-greybuild --write-slot-pool <file.esp>
//   blackflame-greybuild --collisions <request.json>
//   blackflame-greybuild --dump <file.nif> [...]
//   blackflame-greybuild --roundtrip <file.nif> [...]
//   blackflame-greybuild --self-test [--golden-dir <dir>] [--print-golden]
//   blackflame-greybuild --make-golden-fixtures <dir>
// --in folders hold loose files laid out Data-relative (meshes\...); they are searched in
// the order given and the first that has a file wins. --map builds what a fixed map lists
// (the shipped vanilla core); --request finds what one setup needs, builds it and writes
// the map (and, when needed, a small ESL of grey ADDN records) into --out. --forms adds the v2
// copies (GreyForms.h): full-grey copies of the listed forms' NIFs, the copies ESL and the copies
// map, on top of what --request writes (its ADDN list gives the grey indices). --from-game reads the request and the
// NIF dump the DLL wrote at kDataLoaded (GreyFromGame.cpp) and writes all of that into --out, warning and going on
// where a NIF cannot be read. --forms and --from-game also write grey copies of the textures the copy NIFs name
// (GreyTexture.h; --no-textures: none). --write-empty-esls writes the empty copies and local ESLs the mod ships.
// --staging is the single launch's key mode (GreyKeyed.h): request v5's objects at the DLL's keys in a staging folder,
// with progress.jsonl; tint, tintAdd and the palette mode come from the request only. --write-slot-pool writes the slot
// pool ESL, --collisions lists a request's ADDN indices in the pool's range (GreySlotPool.h).
// --self-test also runs the golden-output test (GreySelfTestGolden.cpp) on the committed fixtures in --golden-dir (default
// the repository's tools\blackflame-greybuild\golden; given, it must exist, else a missing folder only skips that part);
// --print-golden runs that part alone and prints this build's table as source. --make-golden-fixtures writes the
// fixtures' input files once (they are committed, never made again at test time).
// Exit 0 = success, 1 = a failure, 2 = bad arguments.

#include "GreyBuild.h"
#include "GreyForms.h"
#include "GreyKeyed.h"
#include "GreySlotPool.h"

#include <charconv>
#include <fmt/format.h>
#include <iostream>
#include <system_error>
#include <stdexcept>
#include <string_view>

namespace
{
    using GreyBuild::kExitFail;
    using GreyBuild::kExitOk;
    using GreyBuild::kExitUsage;
    constexpr std::uint64_t kMinJobMemoryMiB = 64;              // below this no texture of any size is admitted
    constexpr std::uint64_t kMaxMemoryMiB = 1024ull * 1024ull;  // 1 TiB: a typo, not a machine

    struct Options
    {
        std::filesystem::path map;
        std::filesystem::path request;
        std::filesystem::path forms;
        bool fromGame = false;
        std::filesystem::path fromGameDir;  // empty: found under Documents
        bool reclaimRetired = false;
        bool retireLeftOut = false;  // AB5: --from-game: retire copies of forms this request leaves out (their records go)
        bool noTextures = false;  // --no-textures: no texture copies (--forms, --from-game)
        std::filesystem::path emptyEsls;
        std::vector<std::filesystem::path> inputs;
        std::filesystem::path out;
        std::vector<std::filesystem::path> files;  // --dump / --roundtrip
        float tint = GreyBuild::kDefaultTint;
        float tintAdd = GreyBuild::kDefaultTintAdd;  // v2i: additive palette shaders
        GreyBuild::PaletteMode paletteMode = GreyBuild::PaletteMode::kDerived;  // --palette-mode derived|godskin
        bool optionsSet = false;      // --tint, --tint-add or --palette-mode given (refused in key mode)
        std::filesystem::path staging;  // key mode
        std::uint64_t jobMemoryMiB = GreyBuild::kDefaultJobMemoryMiB;
        std::uint64_t commitReserveMiB = GreyBuild::kDefaultCommitReserveMiB;
        std::uint32_t parentPid = 0;
        bool keyedOnlySet = false;    // --job-memory, --commit-reserve or --parent-pid given (key mode only)
        std::filesystem::path slotPool;
        std::filesystem::path collisions;
        bool roundTrip = false;
        bool selfTest = false;
        GreyBuild::SelfTestOptions selfTestOptions;  // --golden-dir, --print-golden (with --self-test only)
        bool selfTestOnlySet = false;
        std::filesystem::path goldenFixtures;        // --make-golden-fixtures
    };

    void PrintUsage()
    {
        std::cerr << "usage:\n"
                     "  blackflame-greybuild --map <map.json> --in <dir> [--in <dir> ...] --out <mod root>\n"
                     "  blackflame-greybuild --request <request.json> --in <dir> [...] --out <mod root> [--tint <0..2>] [--tint-add <0..2>]\n"
                     "  blackflame-greybuild --forms <forms.json> --request <request.json> --in <dir> [...] --out <mod root> "
                     "[--tint <0..2>] [--tint-add <0..2>] [--reclaim-retired] [--no-textures] [--palette-mode derived|godskin]\n"
                     "  blackflame-greybuild --from-game [<request folder>] --out <output mod folder> [--tint <0..2>] [--tint-add <0..2>] "
                     "[--reclaim-retired] [--no-textures] [--palette-mode derived|godskin] [--retire-left-out]\n"
                     "      (the request folder defaults to <Documents>\\My Games\\Skyrim Special Edition[ GOG]\\SKSE\\"
                     "HeartOfMagic_BlackFlame; --out must exist and be the same folder every time; SE and AE, not VR)\n"
                     "      (--no-textures: no grey texture copies; the copy NIFs keep naming the source textures)\n"
                     "      (--palette-mode godskin: GradBlackFlameAdd.dds / GradBlackFlame.dds on every palette shader instead of\n"
                     "       palettes derived from each source palette)\n"
                     "  blackflame-greybuild --from-game [<request folder>] --staging <dir> [--job-memory <MiB>] "
                     "[--commit-reserve <MiB>] [--parent-pid <pid>]\n"
                  << fmt::format("      (key mode, request version 5: tint, tintAdd and the palette mode come from the request; "
                                 "job memory default {} MiB, commit reserve {} MiB)\n",
                         GreyBuild::kDefaultJobMemoryMiB, GreyBuild::kDefaultCommitReserveMiB)
                  << 
                     "  blackflame-greybuild --write-empty-esls <folder>\n"
                     "  blackflame-greybuild --write-slot-pool <file.esp>\n"
                     "  blackflame-greybuild --collisions <request.json>   (exit 0 none, 1 some, 2 the request does not read)\n"
                     "  blackflame-greybuild --dump <file.nif> [...]\n"
                     "  blackflame-greybuild --roundtrip <file.nif> [...]\n"
                     "  blackflame-greybuild --self-test [--golden-dir <dir>] [--print-golden]\n"
                     "  blackflame-greybuild --make-golden-fixtures <dir>\n";
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

    // A whole number of a_min..a_max in a_text: digits only (no space, sign or trailing text); a_value set only then
    [[nodiscard]] bool ParseCount(const char* a_text, std::uint64_t a_min, std::uint64_t a_max, std::uint64_t& a_value)
    {
        const std::string_view text(a_text);
        std::uint64_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || text.front() < '0' || text.front() > '9' || result.ec != std::errc() ||
            result.ptr != text.data() + text.size() || value < a_min || value > a_max) {
            return false;
        }
        a_value = value;
        return true;
    }

    // A path argument that is no empty text (an empty one would pick another mode)
    [[nodiscard]] bool TakePath(const char* a_option, const char* a_text, std::filesystem::path& a_path)
    {
        if (a_text[0] == '\0') {
            std::cerr << fmt::format("{} needs a path, not an empty text\n", a_option);
            return false;
        }
        a_path = a_text;
        return true;
    }

    [[nodiscard]] bool ParseArgs(int a_argc, char** a_argv, Options& a_options)
    {
        for (int i = 1; i < a_argc; ++i) {
            const std::string_view arg = a_argv[i];
            const bool hasValue = i + 1 < a_argc;
            if (arg == "--self-test") {
                a_options.selfTest = true;
            } else if (arg == "--golden-dir" && hasValue) {
                a_options.selfTestOnlySet = true;
                a_options.selfTestOptions.goldenRequired = true;
                if (!TakePath("--golden-dir", a_argv[++i], a_options.selfTestOptions.goldenDir)) {
                    return false;
                }
            } else if (arg == "--print-golden") {
                a_options.selfTestOnlySet = true;
                a_options.selfTestOptions.printGolden = true;
            } else if (arg == "--make-golden-fixtures" && hasValue) {
                if (!TakePath("--make-golden-fixtures", a_argv[++i], a_options.goldenFixtures)) {
                    return false;
                }
            } else if (arg == "--from-game") {
                a_options.fromGame = true;
                if (hasValue && !GreyBuild::StartsWith(a_argv[i + 1], "--")) {
                    a_options.fromGameDir = a_argv[++i];
                }
            } else if (arg == "--no-textures") {
                a_options.noTextures = true;
            } else if (arg == "--palette-mode" && hasValue) {
                a_options.optionsSet = true;
                const std::string_view mode = a_argv[++i];
                if (mode == "derived") {
                    a_options.paletteMode = GreyBuild::PaletteMode::kDerived;
                } else if (mode == "godskin") {
                    a_options.paletteMode = GreyBuild::PaletteMode::kGodskin;
                } else {
                    std::cerr << fmt::format("--palette-mode '{}' must be derived (the default) or godskin\n", mode);
                    return false;
                }
            } else if (arg == "--reclaim-retired") {
                a_options.reclaimRetired = true;
            } else if (arg == "--retire-left-out") {
                a_options.retireLeftOut = true;
            } else if (arg == "--staging" && hasValue) {
                if (!TakePath("--staging", a_argv[++i], a_options.staging)) {
                    return false;
                }
            } else if ((arg == "--job-memory" || arg == "--commit-reserve") && hasValue) {
                const bool job = arg == "--job-memory";
                auto& value = job ? a_options.jobMemoryMiB : a_options.commitReserveMiB;
                if (!ParseCount(a_argv[++i], job ? kMinJobMemoryMiB : 0, kMaxMemoryMiB, value)) {
                    std::cerr << fmt::format("{} '{}' must be a whole number of MiB ({} up to {})\n", arg, a_argv[i],
                        job ? kMinJobMemoryMiB : 0, kMaxMemoryMiB);
                    return false;
                }
                a_options.keyedOnlySet = true;
            } else if (arg == "--parent-pid" && hasValue) {
                std::uint64_t pid = 0;
                if (!ParseCount(a_argv[++i], 1, UINT32_MAX, pid)) {
                    std::cerr << fmt::format("--parent-pid '{}' must be a process id\n", a_argv[i]);
                    return false;
                }
                a_options.parentPid = static_cast<std::uint32_t>(pid);
                a_options.keyedOnlySet = true;
            } else if (arg == "--write-slot-pool" && hasValue) {
                if (!TakePath("--write-slot-pool", a_argv[++i], a_options.slotPool)) {
                    return false;
                }
            } else if (arg == "--collisions" && hasValue) {
                if (!TakePath("--collisions", a_argv[++i], a_options.collisions)) {
                    return false;
                }
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
                a_options.optionsSet = true;
                if (!ParseTint(a_argv[++i], a_options.tint)) {
                    std::cerr << fmt::format("--tint '{}' must be a number in {}..{}\n", a_argv[i],
                        GreyBuild::kMinTint, GreyBuild::kMaxTint);
                    return false;
                }
            } else if (arg == "--tint-add" && hasValue) {
                a_options.optionsSet = true;
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
        const auto request = GreyBuild::LoadFormsRequestFile(a_options.forms);
        if (request.version != GreyBuild::kFormsRequestVersion) {
            std::cerr << fmt::format("{} is a version {} request (the game's): build it with --from-game, which also reads its "
                                     "dump, decal flag and left-out forms. --forms takes hand-made version {} requests.\n",
                a_options.forms.string(), request.version, GreyBuild::kFormsRequestVersion);
            return kExitUsage;
        }
        const auto& sources = request.forms;
        const GreyBuild::Request addons = GreyBuild::LoadRequest(a_options.request);
        std::cout << fmt::format("forms {}: {} source form(s); request {}: {} ADDN record(s), {} model(s)\n",
            a_options.forms.string(), sources.size(), a_options.request.string(), addons.addons.size(),
            addons.models.size());
        GreyBuild::FormsRunOptions options;
        options.reclaimRetired = a_options.reclaimRetired;
        options.tintAdd = a_options.tintAdd;
        options.textures = !a_options.noTextures;
        options.paletteMode = a_options.paletteMode;
        const auto totals = GreyBuild::BuildForms(sources, addons, a_options.inputs, a_options.out, a_options.tint, options);
        std::cout << fmt::format("forms: {} copies, {} copy NIFs, {} failed\n", totals.copies, totals.nifsBuilt,
            totals.failed);
        return totals.failed == 0 && totals.copies > 0 ? kExitOk : kExitFail;
    }
}

namespace
{
    // --from-game --staging: everything that would change an output without changing its key is refused
    [[nodiscard]] int RunKeyMode(const Options& a_options)
    {
        if (!a_options.fromGame || a_options.staging.empty()) {
            std::cerr << "--staging, --job-memory, --commit-reserve and --parent-pid belong to the key mode: --from-game [<request "
                         "folder>] --staging <dir>\n";
            return kExitUsage;
        }
        if (a_options.optionsSet || a_options.noTextures || a_options.reclaimRetired || a_options.retireLeftOut || !a_options.out.empty() ||
            !a_options.inputs.empty() || !a_options.map.empty() || !a_options.request.empty() || !a_options.forms.empty()) {
            std::cerr << "the key mode takes tint, tintAdd and the palette mode from the request (options) and writes only into "
                         "--staging: leave out --tint, --tint-add, --palette-mode, --no-textures, --reclaim-retired, "
                         "--retire-left-out, --out, --in, --map, --request and --forms\n";
            return kExitUsage;
        }
        GreyBuild::KeyedOptions keyed;
        keyed.staging = a_options.staging;
        keyed.jobMemory = a_options.jobMemoryMiB * GreyBuild::kBytesPerMiB;
        keyed.commitReserve = a_options.commitReserveMiB * GreyBuild::kBytesPerMiB;
        keyed.parentPid = a_options.parentPid;
        // the GPU for BC7 targets (GreyGpuEncoder.h); BLACKFLAME_GREYBUILD_GPU=off forces the CPU (tests only)
        keyed.gpu = GreyBuild::GpuUseFromEnvironment(GreyBuild::GpuUse::kAuto);
        try {
            return GreyBuild::RunKeyed(a_options.fromGameDir, keyed);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return kExitFail;
        }
    }
}

int main(int a_argc, char** a_argv)
{
    Options options;
    if (!ParseArgs(a_argc, a_argv, options)) {
        PrintUsage();
        return kExitUsage;
    }
    if (options.selfTestOnlySet && !options.selfTest) {
        std::cerr << "--golden-dir and --print-golden go with --self-test\n";
        return kExitUsage;
    }
    if (options.selfTest) {
        return GreyBuild::RunSelfTest(options.selfTestOptions);
    }
    if (!options.goldenFixtures.empty()) {
        return GreyBuild::MakeGoldenFixtures(options.goldenFixtures);
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
    // one mode a run: the slot pool and the collision list take no other mode's options (agy Opus review)
    const bool otherMode = options.fromGame || !options.staging.empty() || options.keyedOnlySet || !options.out.empty() ||
                           !options.inputs.empty() || !options.map.empty() || !options.request.empty() || !options.forms.empty() ||
                           !options.files.empty() || !options.emptyEsls.empty() || options.optionsSet;
    if ((!options.slotPool.empty() || !options.collisions.empty()) && (otherMode || (!options.slotPool.empty() && !options.collisions.empty()))) {
        std::cerr << "--write-slot-pool and --collisions each run alone: leave out every other mode and option\n";
        return kExitUsage;
    }
    if (!options.slotPool.empty()) {
        return GreyBuild::RunWriteSlotPool(options.slotPool);
    }
    if (!options.collisions.empty()) {
        return GreyBuild::RunCollisions(options.collisions);
    }
    if (!options.staging.empty() || options.keyedOnlySet) {
        return RunKeyMode(options);
    }
    if (options.fromGame) {
        if (!options.inputs.empty() || !options.map.empty() || !options.request.empty() || !options.forms.empty()) {
            std::cerr << "--from-game reads only the game's request and NIF dump: leave out --in, --map, --request and --forms\n";
            return kExitUsage;
        }
        try {
            return GreyBuild::RunFromGame(options.fromGameDir, options.out, options.tint, options.reclaimRetired, options.tintAdd,
                !options.noTextures, options.paletteMode, options.retireLeftOut);
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
