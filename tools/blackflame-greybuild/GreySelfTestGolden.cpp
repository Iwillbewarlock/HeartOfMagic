// =============================================================================
// GreySelfTestGolden - --self-test of the output identity and the golden output (GreySelfTest.h CheckGolden)
// =============================================================================
// The release identity "<n>" promises that every input's output is what version n made. This test holds the promise
// for a fixed fixture (GreyGolden.h): its committed input files, a request written around them, the key mode run with
// the release identity (so a dev build keys and compares the same bytes) and the CPU encoder forced (GpuUse::kOff: the
// GPU's BC7 bytes may change with the driver; guard 5, GreySelfTestGpu.cpp, checks the GPU by tolerance instead).
// Compared with the table of kBuilderOutputVersion
// (GreyGoldenTable.h): every output file's size and fnv64, every progress line's kind, key, outcome and deps (not its
// reason), and a digest of the output-affecting constants. A difference with the same version fails with the
// instruction to bump the version and add a table. The table was made on one machine (FMA3 off, main) and is already the
// build's gate (GreySelfTestGate.cmake); it has not been confirmed on a second machine yet. A released version has a pin
// (GreyGoldenTable.h kReleasedPins, the fnv64 of its table's text, kept apart from the tables --print-golden prints): a
// pinned version whose table is missing or no longer hashes to its pin fails.
// Not in the constant digest: constants inside .cpp files' unnamed namespaces (kVisibleAlpha, the DDS load and
// compress flags); a change to them shows as changed bytes only where the fixture reaches them.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyGolden.h"
#include "GreyGoldenTable.h"
#include "GreyKeyed.h"
#include "GreyRisk.h"
#include "GreySelfTest.h"
#include "GreyShapes.h"
#include "GreyTexStats.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/CopyContractIdentity.h"
#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

namespace GreyBuild::SelfTest
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;
        using namespace Golden;

        constexpr const char* kBumpAdvice = "bump kBuilderOutputVersion in CopyContractKeys.h and regenerate the table "
                                            "(--self-test --print-golden)";
        constexpr std::uint32_t kSlotTextures = 2048;  // PROJ slots
        constexpr std::uint32_t kSlotGlow = 2049;
        constexpr std::uint32_t kSlotPalette = 2050;
        constexpr std::uint32_t kSlotPaletteSrgb = 2051;
        constexpr std::uint32_t kSlotTxst = 3744;      // a TXST slot
        constexpr std::uint32_t kSlotMps = 0xF0C;      // an ADDN slot: index 40012
        constexpr int kMpsIndex = 9;
        constexpr double kGoldenTint = 0.6;     // QuantizedTint values, as the DLL writes them
        constexpr double kGoldenTintAdd = 0.9;
        constexpr std::size_t kTexturesNifCopies = 10;  // bc7 diffuse, bc3, bgra, bc7wide and the six recoloured threshold textures

        struct Observed
        {
            std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> files;  // staging-relative path -> bytes, fnv64
            std::map<std::string, std::string> lines;  // "<kind> <key16>" -> "<outcome> | <deps>"
            std::string constants;
            std::size_t texturesCopies = 0;  // the textures NIF line's path deps
        };

        [[nodiscard]] std::string ReadAll(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        [[nodiscard]] std::uint64_t Fnv(const std::filesystem::path& a_file)
        {
            std::string why;
            const auto digest = DigestFile(a_file, why);
            if (!digest) {
                throw std::runtime_error(fmt::format("golden: {} does not read: {}", PathText(a_file), why));
            }
            return digest->fnv64;
        }

        [[nodiscard]] std::string UpperHex(const std::string& a_bytes)
        {
            std::string out;
            for (const unsigned char c : a_bytes) {
                out += fmt::format("{:02X}", c);
            }
            return out;
        }

        // A TXST slot's record text: the path after textures\ ("" for an empty slot)
        [[nodiscard]] std::string RecordText(const char* a_path)
        {
            return std::string(a_path).substr(std::char_traits<char>::length(kTexturesRoot));
        }

        // A NIF path as a record names it (after meshes\)
        [[nodiscard]] std::string ModelText(const char* a_path)
        {
            return std::string(a_path).substr(std::char_traits<char>::length(kMeshesRoot));
        }

        [[nodiscard]] nlohmann::json Proj(const char* a_source, const char* a_model, const std::string& a_muzzle, std::uint32_t a_slot,
            std::uint64_t a_key)
        {
            return { { "source", a_source }, { "type", "PROJ" }, { "editorId", "" }, { "model", ModelText(a_model) },
                { "muzzleModel", a_muzzle }, { "bounds", { 0, 0, 0, 0, 0, 0 } }, { "data", nlohmann::json::object() },
                { "viaSpell", "Skyrim.esm|0A0000" }, { "costliest", true }, { "slot", a_slot }, { "key", CC::Hex16(a_key) } };
        }

        /// request.json v5 around the dump a_data (its files' sizes and fnv64), keyed with a_identity
        [[nodiscard]] std::string Request(const std::filesystem::path& a_data, const std::string& a_identity)
        {
            CC::KeyPrefix prefix;
            prefix.outputIdentity = a_identity;
            prefix.tint = kGoldenTint;
            prefix.tintAdd = kGoldenTintAdd;
            prefix.gradBlackFlame = CC::FileHash::Hashed(Fnv(a_data / std::string(Rule::kOurPalette)));
            prefix.gradBlackFlameAdd = CC::FileHash::Hashed(Fnv(a_data / std::string(Rule::kOurPaletteAdd)));
            const std::vector<std::string> slots = { RecordText(kDecal), "", "", RecordText(kDecalGlow), "", "", "", "" };
            std::vector<std::string> raw;
            std::transform(slots.begin(), slots.end(), std::back_inserter(raw), UpperHex);
            nlohmann::json forms = nlohmann::json::array();
            forms.push_back(Proj("Skyrim.esm|0A0001", kTexturesNif, ModelText(kBc5Nif), kSlotTextures, kKeyTextures));
            forms.push_back(Proj("Skyrim.esm|0A0002", kGlowNif, "", kSlotGlow, kKeyGlow));
            forms.push_back(Proj("Skyrim.esm|0A0003", kPaletteNif, "", kSlotPalette, kKeyPalette));
            forms.push_back({ { "source", "Skyrim.esm|0A0004" }, { "type", "TXST" }, { "editorId", "GoldenDecal" }, { "model", "" },
                { "bounds", { 0, 0, 0, 0, 0, 0 } }, { "data", { { "flags", 0 } } }, { "hasDecal", true }, { "textures", slots },
                { "texturesRaw", raw }, { "viaSpell", "Skyrim.esm|0A0000" }, { "costliest", true }, { "slot", kSlotTxst } });
            forms.push_back(Proj("Skyrim.esm|0A0005", kPaletteSrgbNif, "", kSlotPaletteSrgb, kKeyPaletteSrgb));
            nlohmann::json nifs = nlohmann::json::array();
            for (const char* nif : { kTexturesNif, kBc5Nif, kGlowNif, kPaletteNif, kMpsNif, kPaletteSrgbNif }) {
                nifs.push_back({ { "path", nif }, { "size", std::filesystem::file_size(a_data / nif) },
                    { "fnv64", CC::Hex16(Fnv(a_data / nif)) }, { "ok", true } });
            }
            nlohmann::json textures = nlohmann::json::array();
            for (const auto& entry : std::filesystem::recursive_directory_iterator(a_data / "textures")) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const auto rel = PathText(entry.path().lexically_relative(a_data).make_preferred());
                const bool own = rel == Rule::kOurPalette || rel == Rule::kOurPaletteAdd;
                textures.push_back({ { "path", rel }, { "size", entry.file_size() }, { "ok", true }, { "state", "ok" }, { "own", own } });
            }
            const nlohmann::json root = { { "version", kGameRequestVersionKeyed }, { "contractVersion", CC::kContractVersion },
                { "game", "1.6.1170.0" }, { "fingerprint", "fnv64:00000000000000ab" }, { "slotsPlugin", "HeartOfMagic_BlackFlame_Slots.esp" },
                { "dumpComplete", true }, { "decalTextureSets", true },
                { "options", { { "tint", prefix.tint }, { "tintAdd", prefix.tintAdd }, { "paletteMode", "derived" },
                                 { "keyPrefix", CC::KeyPrefixString(prefix).value() } } },
                { "forms", forms }, { "muzzles", { { { "path", ModelText(kBc5Nif) }, { "key", CC::Hex16(kKeyBc5) } } } },
                { "addnMps", { { { "slot", kSlotMps }, { "path", ModelText(kMpsNif) }, { "key", CC::Hex16(kKeyMps) }, { "index", kMpsIndex } } } },
                { "txst", { { { "slot", kSlotTxst }, { "key", CC::Hex16(kKeyTxst) }, { "dodt", true } } } },
                { "addons", { { { "index", kMpsIndex }, { "model", ModelText(kMpsNif) }, { "source", "Skyrim.esm|000009" },
                                  { "masterParticleCap", 0 }, { "dnamFlags", 1 }, { "own", false } } } },
                { "nifs", nifs }, { "textures", textures }, { "excluded", nlohmann::json::array() }, { "efshPalettes", GoldenEfshPalettes(a_data, prefix) } };
            return root.dump();
        }

        [[nodiscard]] std::string DepsText(const CC::ProgressLine& a_line)
        {
            std::vector<std::string> deps;
            for (const auto& dep : a_line.deps) {
                deps.push_back(dep.isAddn ? fmt::format("addn:{}", dep.addn) : fmt::format("{}@{}", dep.path, CC::Hex16(dep.fnv64)));
            }
            std::sort(deps.begin(), deps.end());
            std::string out;
            for (const auto& dep : deps) {
                out += (out.empty() ? "" : " ") + dep;
            }
            return out;
        }

        /// Every file in a_staging (progress.jsonl aside) and every progress line
        [[nodiscard]] Observed Observe(const std::filesystem::path& a_staging)
        {
            Observed out;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(a_staging)) {
                if (entry.is_regular_file() && entry.path().filename() != CC::kProgressFile) {
                    out.files[PathText(entry.path().lexically_relative(a_staging).make_preferred())] = { entry.file_size(), Fnv(entry.path()) };
                }
            }
            for (const auto& line : CC::ParseProgress(ReadAll(a_staging / CC::kProgressFile)).lines) {
                const auto deps = DepsText(line);
                out.lines[fmt::format("{} {}", CC::ProgressKindText(line.kind), CC::Hex16(line.key))] =
                    fmt::format("{} | {}", CC::OutcomeText(line.outcome), deps);
                if (line.key == kKeyTextures) {
                    out.texturesCopies = static_cast<std::size_t>(std::count_if(line.deps.begin(), line.deps.end(),
                        [](const CC::ProgressDep& a_dep) { return !a_dep.isAddn; }));
                }
            }
            out.constants = ConstantDigest().digest;
            return out;
        }

        /// A table's canonical text, which its pin hashes: version, constants, every file and every line in array order
        [[nodiscard]] std::string TableText(const GoldenTable& a_table)
        {
            std::string text = fmt::format("version {}\nconstants {}\n", a_table.version, a_table.constants);
            for (std::size_t i = 0; i < a_table.fileCount; ++i) {
                const auto& file = a_table.files[i];
                text += fmt::format("file {}\t{}\t{}\n", file.path, file.bytes, CC::Hex16(file.fnv64));
            }
            for (std::size_t i = 0; i < a_table.lineCount; ++i) {
                const auto& line = a_table.lines[i];
                text += fmt::format("line {}\t{}\t{}\t{}\n", line.kind, line.key, line.outcome, line.deps);
            }
            return text;
        }

        [[nodiscard]] std::string TableFnv(const GoldenTable& a_table)
        {
            return CC::Hex16(CC::Fnv64(TableText(a_table)));
        }

        /// What is wrong with a_pin against a_tables: no table of its version, or one whose text no longer hashes to it;
        /// nullopt when the pin holds
        [[nodiscard]] std::optional<std::string> PinProblem(const ReleasedPin& a_pin, const std::vector<GoldenTable>& a_tables)
        {
            const auto it = std::find_if(a_tables.begin(), a_tables.end(), [&](const GoldenTable& a_t) { return a_t.version == a_pin.version; });
            if (it == a_tables.end()) {
                return fmt::format("golden pin: version {} is pinned ({}) but GreyGoldenTable.h has no table for it: a released table "
                                   "is never removed (restore it from git)", a_pin.version, a_pin.fnv64);
            }
            const auto fnv = TableFnv(*it);
            if (fnv != a_pin.fnv64) {
                return fmt::format("golden pin: the table of released version {} is not the one it was released with (fnv64 {}, pinned "
                                   "{}): a released table is never regenerated; restore it from git, bump kBuilderOutputVersion and add "
                                   "a table", a_pin.version, fnv, a_pin.fnv64);
            }
            return std::nullopt;
        }

        /// Every released version (kReleasedPins) still has the table it was released with ("PASS golden pin: version <n>
        /// is pinned ...", which the package script requires for kBuilderOutputVersion); a table with no pin prints the row
        /// to add ("NOTE golden table v<n> fnv64 <hex>"). Then the check's own cases on made-up tables
        void CheckPins(Checker& a_check)
        {
            const std::vector<GoldenTable> tables(std::begin(kTables), std::end(kTables));
            for (const auto& pin : kReleasedPins) {
                const auto problem = PinProblem(pin, tables);
                a_check.Check(!problem, problem ? *problem :
                                                  fmt::format("golden pin: version {} is pinned and its table holds the pin ({})", pin.version,
                                                      pin.fnv64));
            }
            for (const auto& table : tables) {
                const bool pinned = std::any_of(kReleasedPins.begin(), kReleasedPins.end(),
                    [&](const ReleasedPin& a_pin) { return a_pin.version == table.version; });
                if (!pinned) {
                    const auto fnv = TableFnv(table);
                    std::cout << fmt::format("NOTE golden table v{} fnv64 {} (not pinned: when a tester package with this version ships, "
                                             "add {{ {}, \"{}\" }}, to kReleasedPins in GreyGoldenTable.h)\n", table.version, fnv,
                        table.version, fnv);
                }
            }
            // made-up tables: a pin of its own text holds; another pin, the same rows under another version, or a pinned
            // version with no table, does not
            const GoldenFile files[] = { { "textures\\x.dds", 1, 2 } };
            const GoldenLine lines[] = { { "dds", "0000000000000001", "ok", "" } };
            const GoldenTable made{ 7, "0000000000000003", files, std::size(files), lines, std::size(lines) };
            GoldenTable otherVersion = made;
            ++otherVersion.version;
            const auto madeFnv = TableFnv(made);
            const bool own = !PinProblem({ made.version, madeFnv.c_str() }, { made });
            const bool otherPin = PinProblem({ made.version, "0000000000000000" }, { made }).has_value();
            const bool movedRows = PinProblem({ otherVersion.version, madeFnv.c_str() }, { otherVersion }).has_value();
            const auto missing = PinProblem({ made.version + 1, madeFnv.c_str() }, { made });
            a_check.Check(own && otherPin && movedRows && missing && missing->find("no table") != std::string::npos,
                "golden pin: a table holds the pin of its own text; another pin, another version's text, or a pinned version with no "
                "table fails");
        }

        /// A table as GreyGoldenTable.h would hold it, made from a run (its pointers into a_seen and the vectors here)
        struct OwnTable
        {
            std::vector<GoldenFile> files;
            std::vector<std::string> texts;  // kind, key16, outcome, deps of each line
            std::vector<GoldenLine> lines;
            GoldenTable table{};
        };

        void MakeOwnTable(const Observed& a_seen, OwnTable& a_out)
        {
            for (const auto& [path, file] : a_seen.files) {
                a_out.files.push_back({ path.c_str(), file.first, file.second });
            }
            for (const auto& [name, text] : a_seen.lines) {
                const auto bar = text.find(" | ");
                a_out.texts.insert(a_out.texts.end(), { name.substr(0, name.find(' ')), name.substr(name.find(' ') + 1), text.substr(0, bar),
                                                          text.substr(bar + 3) });
            }
            for (std::size_t i = 0; i + 3 < a_out.texts.size(); i += 4) {
                a_out.lines.push_back({ a_out.texts[i].c_str(), a_out.texts[i + 1].c_str(), a_out.texts[i + 2].c_str(), a_out.texts[i + 3].c_str() });
            }
            a_out.table = { CC::kBuilderOutputVersion, a_seen.constants.c_str(), a_out.files.data(), a_out.files.size(), a_out.lines.data(),
                a_out.lines.size() };
        }

        [[nodiscard]] const GoldenTable* FindTable(std::uint32_t a_version)
        {
            const auto it = std::find_if(std::begin(kTables), std::end(kTables), [&](const GoldenTable& a_t) { return a_t.version == a_version; });
            return it == std::end(kTables) ? nullptr : &*it;
        }

        /// Every difference between a_table and a_seen, each with the instruction; none when they agree
        [[nodiscard]] std::vector<std::string> Compare(const GoldenTable& a_table, const Observed& a_seen)
        {
            std::vector<std::string> out;
            const auto fileText = [](std::uint64_t a_bytes, std::uint64_t a_fnv) { return fmt::format("{} bytes, fnv64 {}", a_bytes, CC::Hex16(a_fnv)); };
            std::map<std::string, std::string> want;
            for (std::size_t i = 0; i < a_table.fileCount; ++i) {
                want[a_table.files[i].path] = fileText(a_table.files[i].bytes, a_table.files[i].fnv64);
            }
            std::map<std::string, std::string> seen;
            for (const auto& [path, file] : a_seen.files) {
                seen[path] = fileText(file.first, file.second);
            }
            const auto diff = [&](const char* a_what, const std::map<std::string, std::string>& a_want, const std::map<std::string, std::string>& a_got) {
                for (const auto& [name, text] : a_want) {
                    const auto it = a_got.find(name);
                    if (it == a_got.end() || it->second != text) {
                        out.push_back(fmt::format("{} {} changed ({} -> {}): {}", a_what, name, text, it == a_got.end() ? "none" : it->second, kBumpAdvice));
                    }
                }
                for (const auto& [name, text] : a_got) {
                    if (a_want.count(name) == 0) {
                        out.push_back(fmt::format("{} {} changed (none -> {}): {}", a_what, name, text, kBumpAdvice));
                    }
                }
            };
            diff("output of", want, seen);
            std::map<std::string, std::string> wantLines;
            for (std::size_t i = 0; i < a_table.lineCount; ++i) {
                const auto& line = a_table.lines[i];
                wantLines[fmt::format("{} {}", line.kind, line.key)] = fmt::format("{} | {}", line.outcome, line.deps);
            }
            diff("progress line of", wantLines, a_seen.lines);
            if (a_seen.constants != a_table.constants) {
                out.push_back(fmt::format("the output-affecting constants changed (digest {} -> {}): {}", a_table.constants, a_seen.constants, kBumpAdvice));
            }
            return out;
        }

        struct GoldenRun
        {
            int exit = -1;
            std::filesystem::path staging;
            Observed seen;
        };

        /// The fixture copied from a_golden\input into a_dir, its request written, the key mode run (release identity)
        [[nodiscard]] GoldenRun RunFixture(const std::filesystem::path& a_dir, const std::filesystem::path& a_golden)
        {
            const auto folder = a_dir / "golden";
            const auto data = folder / "data";
            std::filesystem::remove_all(folder);
            std::filesystem::create_directories(data);
            std::filesystem::copy(a_golden / kInputFolder, data, std::filesystem::copy_options::recursive);
            std::ofstream(folder / "request.json", std::ios::binary) << Request(data, std::to_string(CC::kBuilderOutputVersion));
            GoldenRun run;
            run.staging = folder / "staging";
            KeyedOptions options;
            options.staging = run.staging;
            options.commitReserve = 0;
            options.memoryProbe = []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); };
            options.identityOverride = std::to_string(CC::kBuilderOutputVersion);
            // The CPU encoder, whatever GPU the build machine has: the table holds bytes, and only the CPU's are fixed
            // (GPU output can change with the driver). Explicit, though kOff is KeyedOptions' default
            options.gpu = GpuUse::kOff;
            run.exit = RunKeyed(folder, options);
            run.seen = Observe(run.staging);
            return run;
        }

        /// The CPU encoder ran (no crash marker in staging), and the sRGB source palette's derived copy is sRGB (the
        /// Desaturate branch that decodes to linear and back, GreyPalette.cpp, was reached)
        void CheckCpuAndSrgb(Checker& a_check, const GoldenRun& a_run)
        {
            std::error_code ec;
            a_check.Check(!std::filesystem::exists(a_run.staging / kGpuInflightFile, ec) && !std::filesystem::exists(a_run.staging / kGpuDisabledFile, ec),
                "golden: the run used the CPU encoder only (GpuUse::kOff: no gpu.inflight or gpu.disabled in staging)");
            const auto line = a_run.seen.lines.find(fmt::format("nif {}", CC::Hex16(kKeyPaletteSrgb)));
            std::string palette;
            if (line != a_run.seen.lines.end()) {
                std::istringstream deps(line->second.substr(line->second.find(" | ") + 3));
                for (std::string dep; deps >> dep;) {
                    if (const auto at = dep.find("palette.dds@"); at != std::string::npos) {
                        palette = dep.substr(0, at + std::char_traits<char>::length("palette.dds"));
                    }
                }
            }
            DirectX::TexMetadata meta{};
            const bool read = !palette.empty() &&
                              SUCCEEDED(DirectX::GetMetadataFromDDSFile((a_run.staging / std::filesystem::path(palette)).c_str(),
                                  DirectX::DDS_FLAGS_NONE, meta));
            a_check.Check(read && meta.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                fmt::format("golden: the additive shader on the sRGB source palette got a derived palette stored sRGB ({}, format {})",
                    palette.empty() ? std::string("no palette dep") : palette, read ? static_cast<int>(meta.format) : -1));
        }

        void CheckIdentity(Checker& a_check)
        {
            const auto own = CC::ReadIdentityResource(ThisExecutable());
            a_check.Check(own.text && *own.text == OutputIdentity() && !CC::BuilderIdentityFault(*own.text),
                fmt::format("identity: this exe's RCDATA resource, read as the DLL reads it, is its compiled identity '{}' ({})",
                    OutputIdentity(), own.text ? "'" + *own.text + "'" : own.why));
        }

        void CheckThresholds(Checker& a_check, const std::filesystem::path& a_in, const std::filesystem::path& a_dir)
        {
            for (const auto& fixture : kThresholds) {
                const auto result = RecolourDds(a_in / fixture.path, a_dir / "thresholds" / fixture.path, kTextureTintEffect);
                const auto want = fixture.recoloured ? RecolourOutcome::kRecoloured : RecolourOutcome::kGreyscale;
                a_check.Check(result.outcome == want,
                    fmt::format("golden fixture: {} is {} at tint 1 ({}; chroma {:.4f}, share {:.4f}, risk coloured {})", fixture.path,
                        fixture.recoloured ? "recoloured" : "left as grey", fixture.rule, result.chroma, result.colouredShare,
                        result.riskColoured));
            }
            const auto grey = RecolourDds(a_in / kGreyBc1, a_dir / "thresholds" / kGreyBc1, kTextureTintEffect);
            const auto bc5 = RecolourDds(a_in / kBc5, a_dir / "thresholds" / kBc5, kTextureTintEffect);
            a_check.Check(grey.outcome == RecolourOutcome::kGreyscale && bc5.outcome == RecolourOutcome::kFailed,
                fmt::format("golden fixture: the grey BC1 is left at tint 1 ({}), the BC5 fails ({})", grey.reason, bc5.reason));
        }

        /// The comparison's own failure cases, against a table made from this run (so they hold whether or not the run
        /// matches the released table)
        void CheckComparison(Checker& a_check, const GoldenRun& a_run)
        {
            if (a_run.seen.files.empty() || a_run.seen.lines.empty()) {
                a_check.Check(false, "golden compare: the run wrote no file or no line to compare");
                return;
            }
            OwnTable ownTable;
            MakeOwnTable(a_run.seen, ownTable);
            const GoldenTable& own = ownTable.table;
            a_check.Check(Compare(own, a_run.seen).empty(), "golden compare: a run against its own table has no difference");
            a_check.Check(!PinProblem({ own.version, TableFnv(own).c_str() }, { own }),
                "golden pin: a table made from this run holds the pin of its own text");
            // a deliberately changed byte in a staged copy is a changed output
            const auto& [first, file] = *a_run.seen.files.begin();
            const auto staged = a_run.staging / first;
            auto bytes = ReadAll(staged);
            bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x01);
            std::ofstream(staged, std::ios::binary | std::ios::trunc) << bytes;
            const auto tampered = Compare(own, Observe(a_run.staging));
            a_check.Check(tampered.size() == 1 && tampered.front().find("output of " + first + " changed") != std::string::npos &&
                              tampered.front().find(kBumpAdvice) != std::string::npos,
                fmt::format("golden compare: one changed byte in {} fails with the bump instruction ({} difference(s))", first,
                    tampered.size()));
            auto seen = a_run.seen;
            seen.lines.begin()->second = "failed | " + seen.lines.begin()->second;
            seen.files.erase(seen.files.begin());
            seen.files["textures\\heartofmagic\\blackflame\\copies\\0000000000000000\\extra.dds"] = { 1, 1 };
            seen.constants = "0000000000000000";
            const auto changed = Compare(own, seen);
            const auto has = [&](const std::string& a_text) {
                return std::any_of(changed.begin(), changed.end(), [&](const std::string& a_line) { return a_line.find(a_text) != std::string::npos; });
            };
            a_check.Check(changed.size() == 4 && has("progress line of " + seen.lines.begin()->first) && has("-> none)") &&
                              has("(none -> 1 bytes") && has("constants changed (digest"),
                fmt::format("golden compare: a changed outcome, a file gone, an extra file and changed constants each fail "
                            "({} difference(s))", changed.size()));
            a_check.Check(!FindTable(CC::kBuilderOutputVersion + 1000), "golden compare: a version with no table has none (the run fails)");
        }

        [[nodiscard]] std::string Escaped(const std::string& a_text)
        {
            std::string out;
            for (const char c : a_text) {
                out += c == '\\' || c == '"' ? std::string("\\") + c : std::string(1, c);
            }
            return out;
        }
    }

    void CheckGolden(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir, bool a_required)
    {
        CheckIdentity(a_check);
        CheckPins(a_check);
        if (a_goldenDir.empty()) {
            std::cout << "SKIP golden: no --golden-dir given (only the repository has the fixtures: tools\\blackflame-greybuild\\golden)\n";
            return;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(a_goldenDir / kInputFolder, ec)) {
            if (a_required) {
                a_check.Check(false, fmt::format("golden: {} has no {} folder (the committed fixtures)", PathText(a_goldenDir), kInputFolder));
            } else {
                std::cout << fmt::format("SKIP golden: {} has no {} folder (only the repository has the fixtures)\n", PathText(a_goldenDir),
                    kInputFolder);
            }
            return;
        }
        try {
            const auto started = std::chrono::steady_clock::now();
            CheckThresholds(a_check, a_goldenDir / kInputFolder, a_dir);
            const auto run = RunFixture(a_dir, a_goldenDir);
            a_check.Check(run.exit == 0 && run.seen.texturesCopies == kTexturesNifCopies,
                fmt::format("golden: the key mode ran the fixture (exit {}; {} files, {} lines; the textures NIF has {} texture copies, "
                            "{} expected: the under-threshold and grey ones none)", run.exit, run.seen.files.size(), run.seen.lines.size(),
                    run.seen.texturesCopies, kTexturesNifCopies));
            CheckCpuAndSrgb(a_check, run);
            const auto* table = FindTable(CC::kBuilderOutputVersion);
            if (!table) {
                a_check.Check(false, fmt::format("golden: no table for output version {} in GreyGoldenTable.h: add the one "
                                                 "--self-test --print-golden prints", CC::kBuilderOutputVersion));
                return;
            }
            const auto differences = Compare(*table, run.seen);
            for (const auto& line : differences) {
                std::cout << "      " << line << "\n";
            }
            a_check.Check(differences.empty(), fmt::format("golden: every output file and progress line is version {}'s ({} files, {} "
                                                           "lines, constants {}; {} difference(s))", CC::kBuilderOutputVersion,
                                                   table->fileCount, table->lineCount, table->constants, differences.size()));
            if (!differences.empty() && run.seen.constants != table->constants) {
                std::cout << "      the constants now:\n" << ConstantDigest().text;
            }
            CheckComparison(a_check, run);
            std::cout << fmt::format("NOTE  golden part: {} ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                    std::chrono::steady_clock::now() - started).count());
        } catch (const std::exception& e) {
            a_check.Check(false, fmt::format("golden: exception: {}", e.what()));
        }
    }

    int PrintGolden(const std::filesystem::path& a_dir, const std::filesystem::path& a_goldenDir)
    {
        try {
            std::filesystem::create_directories(a_dir);
            std::ostringstream runOutput;  // the run's own lines, kept out of the table text
            auto* const saved = std::cout.rdbuf(runOutput.rdbuf());
            GoldenRun run;
            try {
                run = RunFixture(a_dir, a_goldenDir);
            } catch (...) {
                std::cout.rdbuf(saved);
                throw;
            }
            std::cout.rdbuf(saved);
            if (run.exit != 0) {
                std::cerr << fmt::format("the key mode run of the fixture exited {}: no table\n", run.exit);
                return kExitFail;
            }
            const auto version = CC::kBuilderOutputVersion;
            const auto constants = ConstantDigest();
            // indented for GreyGoldenTable.h's namespace: the arrays go in as they are, the row into kTables
            std::string out = fmt::format("    // ---- version {} (never edit a released version's arrays)\n    // constants:\n", version);
            std::size_t start = 0;
            for (auto end = constants.text.find('\n'); end != std::string::npos; start = end + 1, end = constants.text.find('\n', start)) {
                out += "    //   " + constants.text.substr(start, end - start) + "\n";
            }
            out += fmt::format("    inline constexpr GoldenFile kFilesV{}[] = {{\n", version);
            for (const auto& [path, file] : run.seen.files) {
                out += fmt::format("        {{ \"{}\", {}, 0x{}ull }},\n", Escaped(path), file.first, CC::Hex16(file.second));
            }
            out += fmt::format("    }};\n    inline constexpr GoldenLine kLinesV{}[] = {{\n", version);
            for (const auto& [name, text] : run.seen.lines) {
                const auto space = name.find(' ');
                const auto bar = text.find(" | ");
                out += fmt::format("        {{ \"{}\", \"{}\", \"{}\", \"{}\" }},\n", name.substr(0, space), name.substr(space + 1),
                    text.substr(0, bar), Escaped(text.substr(bar + 3)));
            }
            // the kTables row only: a released version's pin (kReleasedPins) is never printed, so pasting this cannot overwrite it
            out += fmt::format("    }};\n    // kTables row:\n    //    {{ {}, \"{}\", kFilesV{}, std::size(kFilesV{}), kLinesV{}, "
                               "std::size(kLinesV{}) }},\n", version, constants.digest, version, version, version, version);
            std::cout << out;
            return kExitOk;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return kExitFail;
        }
    }
}
