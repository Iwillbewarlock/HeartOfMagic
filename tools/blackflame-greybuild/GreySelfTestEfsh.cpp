// =============================================================================
// GreySelfTestEfsh - --self-test of the effect shader palettes in key mode (GreySelfTest.h CheckEfsh, contract 6)
// =============================================================================
// The request's efshPalettes is never written by hand here: it is the DLL's own chain (CopyContractEfsh.h, as SlotKeys and
// CopyRequestV5 run it): MergeEfshPalettes over record texts, each path's FileHash, KeyEfshPalettes, EfshPalettesJson.
// A made-up dump holds a fire palette (black to red to yellow, a row per alpha), a blue one, a file that is no DDS, our
// two palettes; one palette is missing. Runs: the first build (the fire one greyed, rows and alpha kept; the others skipped
// and counted), a second one (reused, nothing appended), a dump whose bytes are not the keyed ones (a failed line), and
// requests the reader refuses.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyGolden.h"
#include "GreyKeyed.h"
#include "GreyPalette.h"
#include "GreySelfTest.h"

#include "../../plugins/BlackFlame/src/CopyContractEfsh.h"
#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <fstream>
#include <functional>
#include <sstream>

namespace GreyBuild::SelfTest
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;
        namespace Rule = BlackFlame::DataPathRule;

        constexpr const char* kFire = "textures\\effects\\gradients\\efshfire.dds";
        constexpr const char* kBlue = "textures\\effects\\gradients\\efshblue.dds";
        constexpr const char* kNoDds = "textures\\effects\\gradients\\efshnodds.dds";
        constexpr const char* kMissing = "textures\\effects\\gradients\\efshmissing.dds";
        // review round 1: the fire test's boundary (kEfshFireShare 0.20) both ways, and a palette the game could not read
        constexpr const char* kLow = "textures\\effects\\gradients\\efshlow.dds";    // 2 of 13 lit texels fire: about 0.15
        constexpr const char* kHigh = "textures\\effects\\gradients\\efshhigh.dds";  // 4 of 16: 0.25
        constexpr const char* kUnread = "textures\\effects\\gradients\\efshunread.dds";
        constexpr std::size_t kWidth = 16;
        constexpr std::size_t kRows = 4;
        constexpr std::uint8_t kRowAlphaStep = 60;
        constexpr std::size_t kPalettesListed = 7;  // fire (both roles), blue, the file that is no DDS, the missing one, low, high, unread
        constexpr double kLowShare = 2.0 / 13.0;
        constexpr double kHighShare = 4.0 / 16.0;

        void WriteText(const std::filesystem::path& a_file, const std::string& a_text)
        {
            std::filesystem::create_directories(a_file.parent_path());
            std::ofstream(a_file, std::ios::binary) << a_text;
        }

        [[nodiscard]] std::string ReadAll(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        [[nodiscard]] std::uint64_t Fnv(const std::filesystem::path& a_file)
        {
            std::string why;
            const auto digest = DigestFile(a_file, why);
            return digest ? digest->fnv64 : 0;
        }

        /// A kWidth x kRows palette: a_colour(x) in every row, row y's alpha 255 - y x kRowAlphaStep
        [[nodiscard]] RgbaImage Palette(const std::function<std::array<std::uint8_t, 3>(std::size_t)>& a_colour)
        {
            RgbaImage image;
            image.width = kWidth;
            image.height = kRows;
            image.pixels.resize(kWidth * kRows * 4);
            for (std::size_t y = 0; y < kRows; ++y) {
                for (std::size_t x = 0; x < kWidth; ++x) {
                    const auto rgb = a_colour(x);
                    auto* p = image.pixels.data() + (y * kWidth + x) * 4;
                    std::copy(rgb.begin(), rgb.end(), p);
                    p[3] = static_cast<std::uint8_t>(255 - y * kRowAlphaStep);
                }
            }
            return image;
        }

        // black, red, orange, yellow: x 3..15 fire coloured (hue up to about 53 degrees)
        [[nodiscard]] RgbaImage FirePalette()
        {
            return Palette([](std::size_t x) {
                return std::array<std::uint8_t, 3>{ static_cast<std::uint8_t>(std::min<std::size_t>(255, x * 32)),
                    static_cast<std::uint8_t>(x > 6 ? (x - 6) * 25 : 0), 0 };
            });
        }

        [[nodiscard]] RgbaImage BluePalette()
        {
            return Palette([](std::size_t x) {
                return std::array<std::uint8_t, 3>{ 0, static_cast<std::uint8_t>(x * 8), static_cast<std::uint8_t>(x * 16) };
            });
        }

        constexpr std::array<std::uint8_t, 3> kRed{ 255, 0, 0 };
        constexpr std::array<std::uint8_t, 3> kBlueTexel{ 0, 0, 255 };
        constexpr std::array<std::uint8_t, 3> kBlack{ 0, 0, 0 };

        // 3 black (unlit) columns, 2 red, 11 blue: 2 of 13 lit texels fire coloured, under kEfshFireShare
        [[nodiscard]] RgbaImage LowPalette()
        {
            return Palette([](std::size_t x) { return x < 3 ? kBlack : x < 5 ? kRed : kBlueTexel; });
        }

        // 4 red columns, 12 blue: 4 of 16 lit texels fire coloured, over kEfshFireShare
        [[nodiscard]] RgbaImage HighPalette()
        {
            return Palette([](std::size_t x) { return x < 4 ? kRed : kBlueTexel; });
        }

        void MustWrite(const RgbaImage& a_image, const std::filesystem::path& a_file)
        {
            std::filesystem::create_directories(a_file.parent_path());
            if (std::string why; !WritePaletteDds(a_image, a_file, why)) {
                throw std::runtime_error(fmt::format("efsh fixture: {}", why));
            }
        }

        [[nodiscard]] CC::KeyPrefix Prefix(const std::filesystem::path& a_dump)
        {
            CC::KeyPrefix prefix;
            prefix.outputIdentity = OutputIdentity();
            prefix.tint = 0.6;
            prefix.tintAdd = 0.9;
            prefix.gradBlackFlame = CC::FileHash::Hashed(Fnv(a_dump / std::string(Rule::kOurPalette)));
            prefix.gradBlackFlameAdd = CC::FileHash::Hashed(Fnv(a_dump / std::string(Rule::kOurPaletteAdd)));
            return prefix;
        }

        /// The DLL's chain over record texts, each path's state from a_dump as the DLL's read gives it
        [[nodiscard]] CC::EfshMerge DllPalettes(const std::filesystem::path& a_dump, const CC::KeyPrefix& a_prefix,
            const std::vector<CC::EfshPaletteSource>& a_sources)
        {
            auto merged = CC::MergeEfshPalettes(a_sources);
            for (auto& palette : merged.palettes) {
                const auto file = a_dump / palette.path;
                palette.hash = std::filesystem::exists(file) ? CC::FileHash::Hashed(Fnv(file)) : CC::FileHash::Missing();
            }
            CC::KeyEfshPalettes(merged.palettes, a_prefix);
            return merged;
        }

        [[nodiscard]] std::optional<std::uint64_t> KeyOf(const CC::EfshMerge& a_merge, const char* a_path)
        {
            const auto it = std::find_if(a_merge.palettes.begin(), a_merge.palettes.end(),
                [&](const CC::EfshPalette& a_p) { return a_p.path == a_path; });
            return it == a_merge.palettes.end() ? std::nullopt : it->key;
        }

        struct Fixture
        {
            std::filesystem::path folder;
            std::filesystem::path dump;
            std::filesystem::path staging;
            CC::EfshMerge merge;
        };

        [[nodiscard]] Fixture MakeFixture(const std::filesystem::path& a_dir, const std::string& a_name)
        {
            Fixture fixture{ a_dir / a_name, a_dir / a_name / "data", a_dir / a_name / "staging", {} };
            const auto& dump = fixture.dump;
            if (!WriteTestTexture(dump / std::string(Rule::kOurPalette), DXGI_FORMAT_B8G8R8A8_UNORM, true) ||
                !WriteTestTexture(dump / std::string(Rule::kOurPaletteAdd), DXGI_FORMAT_B8G8R8A8_UNORM, false)) {
                throw std::runtime_error("efsh fixture: our palettes were not written");
            }
            MustWrite(FirePalette(), dump / kFire);
            MustWrite(BluePalette(), dump / kBlue);
            MustWrite(LowPalette(), dump / kLow);
            MustWrite(HighPalette(), dump / kHigh);
            WriteText(dump / kNoDds, "this is no DDS file");
            const auto prefix = Prefix(dump);
            // as the records hold them: one palette in both slots (two spellings), ours, and a path Normalize refuses
            fixture.merge = DllPalettes(dump, prefix, { { "Effects\\Gradients\\EfshFire.dds", CC::EfshRole::kParticle },
                { "effects/gradients/efshfire.dds", CC::EfshRole::kMembrane }, { "Effects\\Gradients\\EfshBlue.dds", CC::EfshRole::kMembrane },
                { "Effects\\Gradients\\EfshNoDds.dds", CC::EfshRole::kParticle }, { "Effects\\Gradients\\EfshMissing.dds", CC::EfshRole::kParticle },
                { "Effects\\Gradients\\EfshLow.dds", CC::EfshRole::kParticle }, { "Effects\\Gradients\\EfshHigh.dds", CC::EfshRole::kMembrane },
                { "Effects\\Gradients\\EfshUnread.dds", CC::EfshRole::kParticle },
                { "HeartOfMagic\\BlackFlame\\GradBlackFlame.dds", CC::EfshRole::kParticle }, { "C:\\Effects\\x.dds", CC::EfshRole::kParticle },
                { "", CC::EfshRole::kMembrane } });
            nlohmann::json textures = nlohmann::json::array();
            for (const char* path : { Rule::kOurPalette.data(), Rule::kOurPaletteAdd.data(), kFire, kBlue, kNoDds, kLow, kHigh }) {
                textures.push_back({ { "path", path }, { "size", std::filesystem::file_size(dump / path) }, { "ok", true }, { "state", "ok" } });
            }
            textures.push_back({ { "path", kMissing }, { "size", 0 }, { "ok", false }, { "state", "missing" }, { "error", "does not open" } });
            // planned, its read failed: the DLL's hash is missing (efshPalettes) and textures[] says unread
            textures.push_back({ { "path", kUnread }, { "size", 64 }, { "ok", false }, { "state", "unread" }, { "error", "read failed" } });
            const nlohmann::json root = { { "version", kGameRequestVersionKeyed }, { "contractVersion", CC::kContractVersion },
                { "game", "1.6.1170.0" }, { "fingerprint", "fnv64:00000000000000ef" }, { "slotsPlugin", "HeartOfMagic_BlackFlame_Slots.esp" },
                { "dumpComplete", true }, { "decalTextureSets", true },
                { "options", { { "tint", prefix.tint }, { "tintAdd", prefix.tintAdd }, { "paletteMode", "derived" },
                                 { "keyPrefix", CC::KeyPrefixString(prefix).value() } } },
                { "forms", { { { "source", "Skyrim.esm|0E0001" }, { "type", "PROJ" }, { "editorId", "" }, { "model", "" }, { "muzzleModel", "" },
                               { "bounds", { 0, 0, 0, 0, 0, 0 } }, { "data", nlohmann::json::object() }, { "viaSpell", "Skyrim.esm|0E0000" },
                               { "costliest", true } } } },
                { "muzzles", nlohmann::json::array() }, { "addnMps", nlohmann::json::array() }, { "txst", nlohmann::json::array() },
                { "efshPalettes", CC::EfshPalettesJson<nlohmann::json>(fixture.merge.palettes) },
                { "addons", { { { "index", 9 }, { "model", "mps\\a.nif" }, { "source", "Skyrim.esm|000009" }, { "masterParticleCap", 0 },
                                  { "dnamFlags", 1 }, { "own", false } } } },
                { "nifs", nlohmann::json::array() }, { "textures", textures }, { "excluded", nlohmann::json::array() } };
            WriteText(fixture.folder / "request.json", root.dump());
            return fixture;
        }

        struct RunResult
        {
            int exit = -1;
            std::string out;  // the run's standard output
        };

        [[nodiscard]] RunResult Run(const Fixture& a_fixture)
        {
            KeyedOptions options;
            options.staging = a_fixture.staging;
            options.commitReserve = 0;
            options.memoryProbe = []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); };
            std::ostringstream captured;
            auto* const old = std::cout.rdbuf(captured.rdbuf());
            RunResult result;
            try {
                result.exit = RunKeyed(a_fixture.folder, options);
            } catch (...) {
                std::cout.rdbuf(old);
                throw;
            }
            std::cout.rdbuf(old);
            result.out = captured.str();
            return result;
        }

        [[nodiscard]] std::vector<CC::ProgressLine> LinesOf(const Fixture& a_fixture, std::optional<std::uint64_t> a_key)
        {
            std::vector<CC::ProgressLine> out;
            for (const auto& line : CC::ParseProgress(ReadAll(a_fixture.staging / CC::kProgressFile)).lines) {
                if (a_key && line.key == *a_key) {
                    out.push_back(line);
                }
            }
            return out;
        }

        void CheckMergeAndShare(Checker& a_check, const Fixture& a_fixture)
        {
            const auto& m = a_fixture.merge;
            const auto fire = std::find_if(m.palettes.begin(), m.palettes.end(), [](const CC::EfshPalette& a_p) { return a_p.path == kFire; });
            a_check.Check(m.palettes.size() == kPalettesListed && m.own == 1 && m.refused == 1 && fire != m.palettes.end() &&
                              fire->role == CC::EfshRole::kBoth && KeyOf(m, kFire) && !KeyOf(m, kMissing),
                fmt::format("efsh merge (the DLL's): {} palettes from 11 record texts (one in both slots, ours and a refused path left out, "
                            "an empty one skipped), the missing one without a key", m.palettes.size()));
            const auto black = Palette([](std::size_t) { return std::array<std::uint8_t, 3>{ 0, 0, 0 }; });
            const auto grey = Palette([](std::size_t x) { const auto v = static_cast<std::uint8_t>(x * 16); return std::array<std::uint8_t, 3>{ v, v, v }; });
            const double fireShare = EfshFireShare(FirePalette());
            a_check.Check(fireShare >= kEfshFireShare && EfshFireShare(BluePalette()) == 0.0 && EfshFireShare(grey) == 0.0 &&
                              EfshFireShare(black) == 0.0,
                fmt::format("efsh fire test: the fire palette {:.3f} (>= {}), blue, grey and black 0", fireShare, kEfshFireShare));
            const double low = EfshFireShare(LowPalette());
            const double high = EfshFireShare(HighPalette());
            a_check.Check(std::abs(low - kLowShare) < 1e-9 && std::abs(high - kHighShare) < 1e-9 && low < kEfshFireShare &&
                              high >= kEfshFireShare,
                fmt::format("efsh fire test at its boundary: {:.3f} (black texels not counted as lit) is under {}, {:.3f} over it",
                    low, kEfshFireShare, high));
        }

        void CheckFirstRuns(Checker& a_check, const Fixture& a_fixture)
        {
            const auto first = Run(a_fixture);
            const auto fireKey = KeyOf(a_fixture.merge, kFire);
            const auto fire = LinesOf(a_fixture, fireKey);
            const auto slot = fireKey ? Rule::PaletteCopyPath(CC::Hex16(*fireKey)) : std::nullopt;
            const bool line = first.exit == 0 && fire.size() == 1 && slot && fire[0].kind == CC::ProgressKind::kDds &&
                              fire[0].outcome == CC::Outcome::kOk && fire[0].path == *slot && fire[0].deps.empty();
            a_check.Check(line, fmt::format("efsh run: exit {}, the fire palette's ok dds line at its key's palette.dds", first.exit));
            std::string why;
            const auto copy = slot ? ReadRgba(a_fixture.staging / *slot, why) : std::nullopt;
            const auto source = FirePalette();
            bool grey = copy && copy->width == kWidth && copy->height == kRows && copy->pixels.size() == source.pixels.size();
            bool lit = false;
            for (std::size_t i = 0; grey && i + 3 < copy->pixels.size(); i += 4) {
                const auto* p = copy->pixels.data() + i;
                grey = p[0] == p[1] && p[1] == p[2] && p[3] == source.pixels[i + 3];
                lit = lit || p[0] > 0;
            }
            a_check.Check(grey && lit, "efsh run: the copy is grey (R = G = B), every texel's alpha is the source's (its rows kept), not black");
            const bool skipped = LinesOf(a_fixture, KeyOf(a_fixture.merge, kBlue)).empty() &&
                                 LinesOf(a_fixture, KeyOf(a_fixture.merge, kNoDds)).empty() && KeyOf(a_fixture.merge, kBlue) &&
                                 !std::filesystem::exists(a_fixture.staging / Rule::PaletteCopyPath(CC::Hex16(*KeyOf(a_fixture.merge, kBlue))).value());
            const std::string summary = "efsh   7 effect shader palette(s): grey 2 written, 0 reused; skipped (the source stays): 2 not fire, "
                                        "1 not decodable, 1 missing in the game, 0 outside the dump caps, 1 unread this start";
            a_check.Check(skipped && first.out.find(summary) != std::string::npos,
                "efsh run: the blue palette (not fire) and the file that is no DDS get no line and no file; the missing one is "
                "counted, the one textures[] lists as unread counted unread (not missing); the summary line says so");
            const auto low = KeyOf(a_fixture.merge, kLow);
            const auto high = KeyOf(a_fixture.merge, kHigh);
            const auto highLines = LinesOf(a_fixture, high);
            const auto highSlot = high ? Rule::PaletteCopyPath(CC::Hex16(*high)) : std::nullopt;
            a_check.Check(low && high && LinesOf(a_fixture, low).empty() &&
                              !std::filesystem::exists(a_fixture.staging / Rule::PaletteCopyPath(CC::Hex16(*low)).value()) &&
                              highLines.size() == 1 && highLines[0].outcome == CC::Outcome::kOk && highSlot && highLines[0].path == *highSlot &&
                              std::filesystem::exists(a_fixture.staging / *highSlot),
                fmt::format("efsh run at the fire test's boundary ({}): the palette at {:.3f} is skipped (no line, no file), the one "
                            "at {:.3f} greyed (an ok dds line, its copy staged)", kEfshFireShare, kLowShare, kHighShare));
            const auto second = Run(a_fixture);
            const auto again = LinesOf(a_fixture, fireKey);
            a_check.Check(second.exit == 0 && again.size() == 1 && second.out.find("grey 0 written, 2 reused") != std::string::npos,
                "efsh run: a second run reuses the staged copy (no new line)");
        }

        void CheckKeyMismatch(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto fixture = MakeFixture(a_dir, "efsh_mismatch");
            MustWrite(Palette([](std::size_t x) { return std::array<std::uint8_t, 3>{ 255, static_cast<std::uint8_t>(x * 8), 0 }; }),
                fixture.dump / kFire);  // other bytes than the key hashed
            const auto run = Run(fixture);
            const auto lines = LinesOf(fixture, KeyOf(fixture.merge, kFire));
            a_check.Check(run.exit == 0 && lines.size() == 1 && lines[0].outcome == CC::Outcome::kFailed && lines[0].path.empty() &&
                              lines[0].reason.find("not the ones the request keyed") != std::string::npos,
                "efsh run: a dump whose palette bytes are not the keyed ones gets a failed line, no copy");
        }

        void CheckRefusals(Checker& a_check, const std::filesystem::path& a_dir, const Fixture& a_fixture)
        {
            const auto root = nlohmann::json::parse(ReadAll(a_fixture.folder / "request.json"));
            const auto refused = [&](const std::string& a_name, const std::function<void(nlohmann::json&)>& a_edit, const char* a_why) {
                auto edited = root;
                a_edit(edited);
                WriteText(a_dir / a_name / "request.json", edited.dump());
                try {
                    (void)LoadKeyedRequest(a_dir / a_name / "request.json");
                    return false;
                } catch (const std::runtime_error& e) {
                    return std::string(e.what()).find(a_why) != std::string::npos;
                }
            };
            const auto entry = [](nlohmann::json& a_root, const char* a_path) -> nlohmann::json& {
                for (auto& item : a_root[CC::Request::kEfshPalettes]) {
                    if (item[CC::Request::kPath] == a_path) {
                        return item;
                    }
                }
                throw std::runtime_error("efsh fixture: no entry");
            };
            a_check.Check(refused("efsh_none", [](nlohmann::json& a_r) { a_r.erase(CC::Request::kEfshPalettes); }, "'efshPalettes' must be an array") &&
                              refused("efsh_cv5", [](nlohmann::json& a_r) { a_r[CC::Request::kContractVersion] = 5; }, "'contractVersion' must be") &&
                              refused("efsh_keymissing", [&](nlohmann::json& a_r) { entry(a_r, kMissing)[CC::Request::kKey] = "0123456789abcdef"; },
                                  "exactly when state is ok") &&
                              refused("efsh_nokey", [&](nlohmann::json& a_r) { entry(a_r, kFire).erase(CC::Request::kKey); }, "exactly when state is ok") &&
                              refused("efsh_twice", [&](nlohmann::json& a_r) { const auto copy = entry(a_r, kBlue); a_r[CC::Request::kEfshPalettes].push_back(copy); },
                                  "listed twice") &&
                              refused("efsh_role", [&](nlohmann::json& a_r) { entry(a_r, kBlue)[CC::Request::kRole] = "edge"; }, "role must be") &&
                              refused("efsh_path", [&](nlohmann::json& a_r) { entry(a_r, kBlue)[CC::Request::kPath] = "Effects\\Blue.dds"; },
                                  "normalised printable ASCII"),
                "efsh request: refused without efshPalettes, at contract 5, with a key on a missing palette or none on an ok one, a "
                "path twice, an unknown role, a path that is not normalised");
            bool writerRefuses = false;
            try {
                (void)CC::EfshPalettesJson<nlohmann::json>({ { "textures\\x.dds", CC::EfshRole::kParticle, CC::FileHash::Hashed(1), std::nullopt } });
            } catch (const std::runtime_error&) {
                writerRefuses = true;
            }
            a_check.Check(writerRefuses, "efsh request: the DLL's writer refuses a hashed palette without a key (never written so)");
        }
    }

    void CheckEfsh(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto fixture = MakeFixture(a_dir, "efsh");
        CheckMergeAndShare(a_check, fixture);
        CheckFirstRuns(a_check, fixture);
        CheckKeyMismatch(a_check, a_dir);
        CheckRefusals(a_check, a_dir, fixture);
    }

    nlohmann::json GoldenEfshPalettes(const std::filesystem::path& a_data, const CC::KeyPrefix& a_prefix)
    {
        constexpr std::size_t kAfterTextures = std::char_traits<char>::length("textures\\");
        const auto merged = DllPalettes(a_data, a_prefix, { { std::string(Golden::kGradFlame).substr(kAfterTextures), CC::EfshRole::kParticle },
            { std::string(Golden::kGreyBc1).substr(kAfterTextures), CC::EfshRole::kMembrane }, { "golden\\efshmissing.dds", CC::EfshRole::kParticle } });
        return CC::EfshPalettesJson<nlohmann::json>(merged.palettes);
    }
}
