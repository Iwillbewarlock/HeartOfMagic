// =============================================================================
// GreySelfTestBases - --self-test of the effect shader base textures in key mode (GreySelfTest.h CheckEfshBases, contract 8)
// =============================================================================
// The request's efshBases is never written by hand here: it is the DLL's own chain (CopyContractEfshBase.h, as SlotKeys
// and CopyRequestV5 run it): EfshBaseRoles over made-up shaders' flags, MergeEfshPalettes over their fill / particle base
// texts, each path's FileHash, KeyEfshBases, EfshBasesJson. A made-up dump holds a fire base (BC3, 64 x 64, 7 mips, an
// alpha ramp), a dark blood red one, two at the fire test's boundary, an sRGB one and a grey one. Runs: the first build
// (the fire one greyed: RGBA8, every mip, alpha exactly the decoded source's; the others skipped and counted), a second
// one (reused), and requests that are refused (no efshBases, contract 7).

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyKeyed.h"
#include "GreyPalette.h"
#include "GreySelfTest.h"

#include "../../plugins/BlackFlame/src/CopyContractEfshBase.h"
#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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

        constexpr const char* kFire = "textures\\effects\\fire\\burnbase.dds";      // BC3 orange, alpha ramp, 7 mips
        constexpr const char* kBlood = "textures\\effects\\blood\\drip.dds";        // red at value 0.451: not fire
        constexpr const char* kLow = "textures\\effects\\fire\\low.dds";            // 12 of 256 fire texels: 0.0469
        constexpr const char* kHigh = "textures\\effects\\fire\\high.dds";          // 13 of 256: 0.0508
        constexpr const char* kSrgb = "textures\\effects\\fire\\srgbbase.dds";      // orange, R8G8B8A8_UNORM_SRGB
        constexpr const char* kGrey = "textures\\effects\\smoke\\grey.dds";         // BC3 grey: not fire
        constexpr const char* kPaletteFill = "textures\\effects\\fire\\palettefill.dds";  // greyscale to colour: never listed
        constexpr const char* kInactive = "textures\\effects\\fire\\inactive.dds";        // membrane off: never listed
        constexpr std::size_t kSide = 16;
        constexpr std::size_t kLowFire = 12;
        constexpr std::size_t kHighFire = 13;
        constexpr std::size_t kFireMips = 7;  // WriteTestTexture's 64 x 64 chain

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

        /// kSide x kSide: the first a_fire texels a_fireColour, the rest blue; alpha a_alpha everywhere
        [[nodiscard]] RgbaImage Mixed(std::size_t a_fire, std::array<std::uint8_t, 3> a_fireColour, std::uint8_t a_alpha = 255,
            std::uint8_t a_otherAlpha = 255)
        {
            RgbaImage image;
            image.width = kSide;
            image.height = kSide;
            image.pixels.resize(kSide * kSide * 4);
            for (std::size_t i = 0; i < kSide * kSide; ++i) {
                auto* p = image.pixels.data() + i * 4;
                const bool fire = i < a_fire;
                const std::array<std::uint8_t, 3> rgb = fire ? a_fireColour : std::array<std::uint8_t, 3>{ 0, 0, 255 };
                std::copy(rgb.begin(), rgb.end(), p);
                p[3] = fire ? a_alpha : a_otherAlpha;
            }
            return image;
        }

        constexpr std::array<std::uint8_t, 3> kOrange{ 255, 128, 0 };
        constexpr std::array<std::uint8_t, 3> kDarkRed{ 115, 0, 0 };    // value 0.451: fx_blooddrip's brightest red
        constexpr std::array<std::uint8_t, 3> kBrightRed{ 141, 0, 0 };  // value 0.553: over kEfshBaseRedMinValue

        void MustWrite(const RgbaImage& a_image, const std::filesystem::path& a_file)
        {
            std::filesystem::create_directories(a_file.parent_path());
            if (std::string why; !WritePaletteDds(a_image, a_file, why)) {
                throw std::runtime_error(fmt::format("base fixture: {}", why));
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

        /// A made-up shader as SlotKeys reads one: its flags and its two base texts
        struct Shader
        {
            CC::EfshShaderFlags flags;
            std::string fill;
            std::string particle;
        };

        /// The DLL's chain (SlotKeys ReadEfshPalettes, Read): roles by EfshBaseRoles, merged, hashed from a_dump, keyed
        [[nodiscard]] CC::EfshMerge DllBases(const std::filesystem::path& a_dump, const CC::KeyPrefix& a_prefix, const std::vector<Shader>& a_shaders)
        {
            std::vector<CC::EfshPaletteSource> sources;
            for (const auto& shader : a_shaders) {
                const auto roles = CC::EfshBaseRoles(shader.flags);
                if (CC::EfshRoleHas(roles, CC::EfshRole::kParticle)) {
                    sources.push_back({ shader.particle, CC::EfshRole::kParticle });
                }
                if (CC::EfshRoleHas(roles, CC::EfshRole::kMembrane)) {
                    sources.push_back({ shader.fill, CC::EfshRole::kMembrane });
                }
            }
            auto merged = CC::MergeEfshPalettes(sources);
            for (auto& base : merged.palettes) {
                const auto file = a_dump / base.path;
                base.hash = std::filesystem::exists(file) ? CC::FileHash::Hashed(Fnv(file)) : CC::FileHash::Missing();
            }
            CC::KeyEfshBases(merged.palettes, a_prefix);
            return merged;
        }

        [[nodiscard]] const CC::EfshPalette* Entry(const CC::EfshMerge& a_merge, const char* a_path)
        {
            const auto it = std::find_if(a_merge.palettes.begin(), a_merge.palettes.end(), [&](const CC::EfshPalette& a_p) { return a_p.path == a_path; });
            return it == a_merge.palettes.end() ? nullptr : &*it;
        }

        [[nodiscard]] std::optional<std::uint64_t> KeyOf(const CC::EfshMerge& a_merge, const char* a_path)
        {
            const auto* entry = Entry(a_merge, a_path);
            return entry ? entry->key : std::nullopt;
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
                !WriteTestTexture(dump / std::string(Rule::kOurPaletteAdd), DXGI_FORMAT_B8G8R8A8_UNORM, false) ||
                !WriteTestTexture(dump / kFire, DXGI_FORMAT_BC3_UNORM, true) || !WriteTestTexture(dump / kGrey, DXGI_FORMAT_BC3_UNORM, false) ||
                !WriteTestTexture(dump / kSrgb, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true) ||
                !WriteTestTexture(dump / kPaletteFill, DXGI_FORMAT_BC3_UNORM, true) || !WriteTestTexture(dump / kInactive, DXGI_FORMAT_BC3_UNORM, true)) {
                throw std::runtime_error("base fixture: a test texture was not written");
            }
            MustWrite(Mixed(kSide * kSide, kDarkRed), dump / kBlood);
            MustWrite(Mixed(kLowFire, kOrange), dump / kLow);
            MustWrite(Mixed(kHighFire, kOrange), dump / kHigh);
            const auto prefix = Prefix(dump);
            CC::EfshShaderFlags membrane;  // membrane on, no palette; particle off
            membrane.disableParticleShader = true;
            CC::EfshShaderFlags palette = membrane;
            palette.greyscaleToColor = true;
            CC::EfshShaderFlags off = membrane;
            off.disableTextureShader = true;
            CC::EfshShaderFlags particle;  // particle on, no palette; membrane off
            particle.disableTextureShader = true;
            fixture.merge = DllBases(dump, prefix, { { membrane, "Effects\\Fire\\BurnBase.dds", "" }, { particle, "", "effects/fire/burnbase.dds" },
                { palette, "Effects\\Fire\\PaletteFill.dds", "" }, { off, "Effects\\Fire\\Inactive.dds", "" }, { membrane, "Effects\\Blood\\Drip.dds", "" },
                { membrane, "Effects\\Fire\\Low.dds", "" }, { membrane, "Effects\\Fire\\High.dds", "" }, { particle, "", "Effects\\Fire\\SrgbBase.dds" },
                { membrane, "Effects\\Smoke\\Grey.dds", "" }, { membrane, "Effects\\Fire\\Missing.dds", "" } });
            nlohmann::json textures = nlohmann::json::array();
            for (const char* path : { Rule::kOurPalette.data(), Rule::kOurPaletteAdd.data(), kFire, kBlood, kLow, kHigh, kSrgb, kGrey }) {
                textures.push_back({ { "path", path }, { "size", std::filesystem::file_size(dump / path) }, { "ok", true }, { "state", "ok" } });
            }
            textures.push_back({ { "path", "textures\\effects\\fire\\missing.dds" }, { "size", 0 }, { "ok", false }, { "state", "missing" },
                { "error", "does not open" } });
            const nlohmann::json root = { { "version", kGameRequestVersionKeyed }, { "contractVersion", CC::kContractVersion },
                { "game", "1.6.1170.0" }, { "fingerprint", "fnv64:00000000000000ee" }, { "slotsPlugin", "HeartOfMagic_BlackFlame_Slots.esp" },
                { "dumpComplete", true }, { "decalTextureSets", true },
                { "options", { { "tint", prefix.tint }, { "tintAdd", prefix.tintAdd }, { "paletteMode", "derived" },
                                 { "keyPrefix", CC::KeyPrefixString(prefix).value() } } },
                { "forms", { { { "source", "Skyrim.esm|0E0001" }, { "type", "PROJ" }, { "editorId", "" }, { "model", "" }, { "muzzleModel", "" },
                               { "bounds", { 0, 0, 0, 0, 0, 0 } }, { "data", nlohmann::json::object() }, { "viaSpell", "Skyrim.esm|0E0000" },
                               { "costliest", true } } } },
                { "muzzles", nlohmann::json::array() }, { "addnMps", nlohmann::json::array() }, { "txst", nlohmann::json::array() },
                { "efshPalettes", nlohmann::json::array() }, { "efshBases", CC::EfshBasesJson<nlohmann::json>(fixture.merge.palettes) },
                { "addons", { { { "index", 9 }, { "model", "mps\\a.nif" }, { "source", "Skyrim.esm|000009" }, { "masterParticleCap", 0 },
                                  { "dnamFlags", 1 }, { "own", false } } } },
                { "nifs", nlohmann::json::array() }, { "textures", textures }, { "excluded", nlohmann::json::array() } };
            WriteText(fixture.folder / "request.json", root.dump());
            return fixture;
        }

        struct RunResult
        {
            int exit = -1;
            std::string out;
        };

        [[nodiscard]] RunResult Run(const std::filesystem::path& a_folder, const std::filesystem::path& a_staging,
            std::uint64_t a_copyCap = CC::kMaxBaseCopyBytes)
        {
            KeyedOptions options;
            options.staging = a_staging;
            options.baseCopyCap = a_copyCap;
            options.commitReserve = 0;
            options.memoryProbe = []() { return std::optional<MemoryState>(MemoryState{ 0, UINT64_MAX }); };
            std::ostringstream captured;
            auto* const old = std::cout.rdbuf(captured.rdbuf());
            RunResult result;
            try {
                result.exit = RunKeyed(a_folder, options);
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

        void CheckSelectionAndShare(Checker& a_check, const Fixture& a_fixture)
        {
            const auto& m = a_fixture.merge;
            const auto* fire = Entry(m, kFire);
            a_check.Check(fire && fire->role == CC::EfshRole::kBoth && fire->key && !Entry(m, kPaletteFill) && !Entry(m, kInactive) &&
                              m.palettes.size() == 7 && !KeyOf(m, "textures\\effects\\fire\\missing.dds"),
                fmt::format("efsh base selection (the DLL's chain): {} entries; the fire base named as one shader's fill and another's "
                            "particle base is one entry (both); a greyscale-to-colour fill and a disabled membrane's are never listed; "
                            "the missing one has no key", m.palettes.size()));
            const double low = EfshBaseFireShare(Mixed(kLowFire, kOrange));
            const double high = EfshBaseFireShare(Mixed(kHighFire, kOrange));
            a_check.Check(low < kEfshBaseFireShare && high >= kEfshBaseFireShare && std::abs(low - 12.0 / 256.0) < 1e-9 &&
                              std::abs(high - 13.0 / 256.0) < 1e-9,
                fmt::format("efsh base fire test at its boundary ({}): {:.4f} under, {:.4f} over", kEfshBaseFireShare, low, high));
            const double dark = EfshBaseFireShare(Mixed(kSide * kSide, kDarkRed));
            const double bright = EfshBaseFireShare(Mixed(kSide * kSide, kBrightRed));
            // half the texels orange at alpha 64, half blue at 255: 64 / (64 + 255) of the visible weight
            const double weighted = EfshBaseFireShare(Mixed(kSide * kSide / 2, kOrange, 64, 255));
            const double invisible = EfshBaseFireShare(Mixed(kSide * kSide, kOrange, 0));
            a_check.Check(dark == 0.0 && bright == 1.0 && std::abs(weighted - 64.0 / 319.0) < 1e-9 && invisible == 0.0,
                fmt::format("efsh base fire test: blood red at value 0.451 is not fire, red at 0.553 is (the floor {}); alpha weighted "
                            "({:.4f}); a texture with no visible texel is 0", kEfshBaseRedMinValue, weighted));
        }

        /// The source's chain decoded to RGBA8 as the pass decodes it, or empty
        [[nodiscard]] DirectX::ScratchImage DecodedSource(const std::filesystem::path& a_file)
        {
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage source;
            DirectX::ScratchImage out;
            if (FAILED(DirectX::LoadFromDDSFile(a_file.c_str(), DirectX::DDS_FLAGS_NONE, &meta, source)) ||
                FAILED(DirectX::Decompress(source.GetImages(), source.GetImageCount(), meta, DXGI_FORMAT_R8G8B8A8_UNORM, out))) {
                return {};
            }
            return out;
        }

        /// The copy against the decoded source: RGBA8, every mip, alpha exact, R = G = B, some texel lit
        [[nodiscard]] bool CopyHolds(const std::filesystem::path& a_copy, const std::filesystem::path& a_source, std::string& a_what)
        {
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage copy;
            const auto source = DecodedSource(a_source);
            if (FAILED(DirectX::LoadFromDDSFile(a_copy.c_str(), DirectX::DDS_FLAGS_NONE, &meta, copy)) || source.GetImageCount() == 0) {
                a_what = "the copy or the source does not read";
                return false;
            }
            bool alpha = true;
            bool grey = true;
            bool lit = false;
            bool alphaVaries = false;
            for (std::size_t i = 0; i < copy.GetImageCount() && i < source.GetImageCount(); ++i) {
                const auto& c = copy.GetImages()[i];
                const auto& s = source.GetImages()[i];
                for (std::size_t y = 0; y < c.height; ++y) {
                    for (std::size_t x = 0; x < c.width; ++x) {
                        const auto* p = c.pixels + y * c.rowPitch + x * 4;
                        const auto* q = s.pixels + y * s.rowPitch + x * 4;
                        alpha = alpha && p[3] == q[3];
                        grey = grey && p[0] == p[1] && p[1] == p[2];
                        lit = lit || p[0] > 0;
                        alphaVaries = alphaVaries || p[3] != s.pixels[3];
                    }
                }
            }
            a_what = fmt::format("format {}, {} mips (source {}), alpha exact {}, grey {}, lit {}, the source's alpha varies {}",
                static_cast<int>(meta.format), meta.mipLevels, source.GetMetadata().mipLevels, alpha, grey, lit, alphaVaries);
            return meta.format == DXGI_FORMAT_R8G8B8A8_UNORM && meta.mipLevels == kFireMips && source.GetMetadata().mipLevels == kFireMips &&
                   copy.GetImageCount() == source.GetImageCount() && alpha && grey && lit && alphaVaries;
        }

        void CheckRuns(Checker& a_check, const Fixture& a_fixture)
        {
            const auto first = Run(a_fixture.folder, a_fixture.staging);
            const auto fireKey = KeyOf(a_fixture.merge, kFire);
            const auto fire = LinesOf(a_fixture, fireKey);
            const auto slot = fireKey ? Rule::CopyPath(Rule::kTexturesRoot, CC::Hex16(*fireKey), kFire) : std::nullopt;
            const auto parts = slot ? Rule::ParseCopyPath(*slot) : std::nullopt;
            a_check.Check(first.exit == 0 && fire.size() == 1 && slot && parts && !parts->palette && fire[0].kind == CC::ProgressKind::kDds &&
                              fire[0].outcome == CC::Outcome::kOk && fire[0].path == *slot,
                fmt::format("efsh base run: exit {}, the fire base's ok dds line at its key's copy.dds", first.exit));
            std::string what;
            a_check.Check(slot && CopyHolds(a_fixture.staging / *slot, a_fixture.dump / kFire, what),
                "efsh base run: the copy is RGBA8 with all 7 mips, every mip's alpha exactly the decoded BC3 source's (a ramp), "
                "R = G = B, not black: " + what);
            const auto noLine = [&](const char* a_path) {
                const auto key = KeyOf(a_fixture.merge, a_path);
                return key && LinesOf(a_fixture, key).empty() &&
                       !std::filesystem::exists(a_fixture.staging / Rule::CopyPath(Rule::kTexturesRoot, CC::Hex16(*key), a_path).value());
            };
            const auto high = LinesOf(a_fixture, KeyOf(a_fixture.merge, kHigh));
            const std::string summary = "bases  7 effect shader base texture(s): grey 2 written";
            const std::string skips = "skipped (the source stays): 3 not fire (0 known from the cache), 0 over the copies cap, 1 format "
                                      "left out or not decodable (1 sRGB), 1 missing in the game";
            a_check.Check(noLine(kBlood) && noLine(kLow) && noLine(kSrgb) && noLine(kGrey) && high.size() == 1 && high[0].outcome == CC::Outcome::kOk &&
                              first.out.find(summary) != std::string::npos && first.out.find(skips) != std::string::npos,
                "efsh base run: the blood red, the one under the boundary, the sRGB and the grey base get no line and no file; the one "
                "over it is greyed; the summary counts them (sRGB apart)");
            const auto second = Run(a_fixture.folder, a_fixture.staging);
            std::size_t cached = 0;
            std::istringstream cache(ReadAll(a_fixture.staging / "efshbase-notfire.txt"));
            for (std::string line; std::getline(cache, line);) {
                cached += line.empty() ? 0 : 1;
            }
            a_check.Check(second.exit == 0 && LinesOf(a_fixture, fireKey).size() == 1 && second.out.find("grey 0 written (0.0 MiB), 2 reused") != std::string::npos &&
                              second.out.find("3 not fire (3 known from the cache)") != std::string::npos && cached == 3 &&
                              second.out.find("EBASE SKIP textures\\effects\\blood\\drip.dds: not fire") == std::string::npos,
                fmt::format("efsh base run: a second run reuses the staged copies (no new line) and skips the 3 bases that are not "
                            "fire from the builder's staging cache ({} lines) without reading them", cached));
        }

        // The base copies' total cap (kMaxBaseCopyBytes; the seam here): a fire base whose copy would pass it is skipped and
        // counted, a smaller one after it still fits
        void CheckCopyCap(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto fixture = MakeFixture(a_dir, "efshbase_cap");
            constexpr std::uint64_t kCap = 5000;  // the 16 x 16 base (1 172 bytes) fits; the 64 x 64 chain (21 992) does not
            const auto run = Run(fixture.folder, fixture.staging, kCap);
            const auto high = LinesOf(fixture, KeyOf(fixture.merge, kHigh));
            a_check.Check(run.exit == 0 && LinesOf(fixture, KeyOf(fixture.merge, kFire)).empty() && high.size() == 1 &&
                              high[0].outcome == CC::Outcome::kOk && run.out.find("1 over the copies cap") != std::string::npos &&
                              run.out.find("would take the base copies past their cap") != std::string::npos,
                "efsh base copies cap: the 64 x 64 fire base over a 5000-byte cap is skipped with its reason (no line), the 16 x 16 "
                "one after it is written; the summary counts the skip");
        }

        void CheckRefusals(Checker& a_check, const std::filesystem::path& a_dir, const Fixture& a_fixture)
        {
            const auto root = nlohmann::json::parse(ReadAll(a_fixture.folder / "request.json"));
            const auto write = [&](const std::string& a_name, const std::function<void(nlohmann::json&)>& a_edit) {
                auto edited = root;
                a_edit(edited);
                WriteText(a_dir / a_name / "request.json", edited.dump());
                return a_dir / a_name;
            };
            const auto throws = [](const std::function<void()>& a_read, const char* a_why) {
                try {
                    a_read();
                    return false;
                } catch (const std::runtime_error& e) {
                    return std::string(e.what()).find(a_why) != std::string::npos;
                }
            };
            const auto none = write("ebase_none", [](nlohmann::json& a_r) { a_r.erase(CC::Request::kEfshBases); });
            const auto cv7 = write("ebase_cv7", [](nlohmann::json& a_r) { a_r[CC::Request::kContractVersion] = CC::kContractVersion - 1; });
            const auto role = write("ebase_role", [](nlohmann::json& a_r) { a_r[CC::Request::kEfshBases][0][CC::Request::kRole] = "membrane"; });
            const bool noneRefused = throws([&]() { (void)LoadKeyedRequest(none / "request.json"); }, "'efshBases' must be an array (contract 8)") &&
                                     Run(none, a_dir / "ebase_none_staging").exit == 2;
            const bool cvRefused = throws([&]() { (void)LoadKeyedRequest(cv7 / "request.json"); }, "'contractVersion' must be 8") &&
                                   Run(cv7, a_dir / "ebase_cv7_staging").exit == 2;
            const bool roleRefused = throws([&]() { (void)LoadKeyedRequest(role / "request.json"); }, "role must be membrane-fill, particle-base or both");
            a_check.Check(noneRefused && cvRefused && roleRefused,
                "efsh base request: a contract 8 builder refuses a request without efshBases and one at contract 7 (exit 2, nothing "
                "built), and a palette role text in efshBases");
        }
    }

    void CheckEfshBases(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto fixture = MakeFixture(a_dir, "efshbase");
        CheckSelectionAndShare(a_check, fixture);
        CheckRuns(a_check, fixture);
        CheckCopyCap(a_check, a_dir);
        CheckRefusals(a_check, a_dir, fixture);
    }
}
