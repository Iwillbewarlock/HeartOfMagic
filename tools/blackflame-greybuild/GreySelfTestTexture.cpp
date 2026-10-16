// =============================================================================
// GreySelfTestTexture - --self-test of the texture copies (GreyTexture.h) on made-up DDS files and a made-up NIF
// =============================================================================
// Coloured BC3 / BC7 / B8G8R8A8 textures become grey with alpha, format and mips kept and the source untouched; a grey
// BC1 is skipped; a broken DDS and a BC5 fail cleanly; the NIF slot rule (lighting diffuse and glow, effect source off
// the palette) repoints only the slots whose texture got a copy, each texture made once per run.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreySelfTest.h"
#include "GreyTexture.h"

#include <DirectXTex.h>
#include <NifFile.hpp>
#include <Shaders.hpp>

#include <cstdlib>
#include <fmt/format.h>
#include <fstream>
#include <memory>

namespace GreyBuild::SelfTest
{
    namespace
    {
        constexpr std::size_t kTestSize = 64;     // 7 mips
        constexpr std::size_t kTestMips = 7;
        constexpr int kChannelTolerance = 10;     // 8-bit steps a BC encode may move a texel
        constexpr float kTestTint = 0.6f;
        constexpr std::uint8_t kOrange[3] = { 255, 128, 0 };
        constexpr std::uint8_t kGrey = 128;
        constexpr std::uint8_t kAlphaFloor = 40;  // the test alpha runs from here up across each row

        enum class Fill
        {
            kOrange,
            kGrey
        };

        [[nodiscard]] std::uint8_t TestAlpha(std::size_t a_x) noexcept
        {
            return static_cast<std::uint8_t>(kAlphaFloor + (255 - kAlphaFloor) * a_x / (kTestSize - 1));
        }

        // A kTestSize square with a full mip chain, filled orange or grey, alpha a gradient, written in a_format
        [[nodiscard]] bool WriteDds(const std::filesystem::path& a_file, DXGI_FORMAT a_format, Fill a_fill)
        {
            DirectX::ScratchImage base;
            if (FAILED(base.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, kTestSize, kTestSize, 1, 1))) {
                return false;
            }
            const DirectX::Image& image = *base.GetImage(0, 0, 0);
            for (std::size_t y = 0; y < kTestSize; ++y) {
                std::uint8_t* row = image.pixels + y * image.rowPitch;
                for (std::size_t x = 0; x < kTestSize; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        row[x * 4 + c] = a_fill == Fill::kOrange ? kOrange[c] : kGrey;
                    }
                    row[x * 4 + 3] = TestAlpha(x);
                }
            }
            DirectX::ScratchImage mips;
            const auto filter = static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_FORCE_NON_WIC);
            if (FAILED(DirectX::GenerateMipMaps(image, filter, 0, mips))) {
                return false;
            }
            DirectX::ScratchImage encoded;
            HRESULT hr = S_OK;
            if (DirectX::IsCompressed(a_format)) {
                hr = DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), a_format,
                    static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK),
                    DirectX::TEX_THRESHOLD_DEFAULT, encoded);
            } else {
                hr = DirectX::Convert(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), a_format, filter,
                    DirectX::TEX_THRESHOLD_DEFAULT, encoded);
            }
            std::filesystem::create_directories(a_file.parent_path());
            return SUCCEEDED(hr) && SUCCEEDED(DirectX::SaveToDDSFile(encoded.GetImages(), encoded.GetImageCount(),
                                        encoded.GetMetadata(), DirectX::DDS_FLAGS_NONE, a_file.c_str()));
        }

        struct Mip0
        {
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            std::size_t mips = 0;
            std::vector<std::uint8_t> rgba;  // kTestSize x kTestSize x 4
        };

        // A DDS's format, mip count and mip 0 as 8-bit RGBA
        [[nodiscard]] bool ReadMip0(const std::filesystem::path& a_file, Mip0& a_out)
        {
            DirectX::TexMetadata meta{};
            DirectX::ScratchImage image;
            if (FAILED(DirectX::LoadFromDDSFile(a_file.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image))) {
                return false;
            }
            a_out.format = meta.format;
            a_out.mips = meta.mipLevels;
            DirectX::ScratchImage rgba;
            const DirectX::Image& top = *image.GetImage(0, 0, 0);
            const HRESULT hr = DirectX::IsCompressed(meta.format) ?
                                   DirectX::Decompress(top, DXGI_FORMAT_R8G8B8A8_UNORM, rgba) :
                                   DirectX::Convert(top, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_FORCE_NON_WIC,
                                       DirectX::TEX_THRESHOLD_DEFAULT, rgba);
            if (FAILED(hr)) {
                return false;
            }
            const DirectX::Image& pixels = *rgba.GetImage(0, 0, 0);
            a_out.rgba.clear();
            for (std::size_t y = 0; y < pixels.height; ++y) {
                const std::uint8_t* row = pixels.pixels + y * pixels.rowPitch;
                a_out.rgba.insert(a_out.rgba.end(), row, row + pixels.width * 4);
            }
            return true;
        }

        [[nodiscard]] std::string Bytes(const std::filesystem::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        // Every texel of a_copy grey at a_expected, its alpha within tolerance of a_source's
        [[nodiscard]] bool GreyWithAlpha(const Mip0& a_copy, const Mip0& a_source, int a_expected, std::string& a_why)
        {
            if (a_copy.rgba.size() != a_source.rgba.size() || a_copy.rgba.empty()) {
                a_why = "sizes differ";
                return false;
            }
            for (std::size_t i = 0; i < a_copy.rgba.size(); i += 4) {
                const int r = a_copy.rgba[i];
                const int g = a_copy.rgba[i + 1];
                const int b = a_copy.rgba[i + 2];
                const int a = a_copy.rgba[i + 3];
                if (std::abs(r - g) > kChannelTolerance || std::abs(g - b) > kChannelTolerance ||
                    std::abs(g - a_expected) > kChannelTolerance || std::abs(a - a_source.rgba[i + 3]) > kChannelTolerance) {
                    a_why = fmt::format("texel {}: {} {} {} {} (expected grey {}, alpha {})", i / 4, r, g, b, a, a_expected,
                        a_source.rgba[i + 3]);
                    return false;
                }
            }
            return true;
        }

        void CheckPaths(Checker& a_check)
        {
            a_check.Check(TextureDataPath("Textures/Effects/FXFire01.DDS") == std::string("textures\\effects\\fxfire01.dds") &&
                              TextureDataPath("effects\\a.dds") == std::string("textures\\effects\\a.dds") &&
                              TextureDataPath("data\\textures\\b.dds") == std::string("textures\\b.dds") &&
                              TextureDataPath("\\Textures\\textures\\c.dds") == std::string("textures\\c.dds"),
                "textures: slot text as a Data-relative path (case, slashes, data\\ and repeated textures\\ dropped, one "
                "textures\\ in front, as the DLL's dump path)");
            a_check.Check(!TextureDataPath("..\\x.dds") && !TextureDataPath("c:\\x.dds") && !TextureDataPath("textures\\x.tga") &&
                              !TextureDataPath(""),
                "textures: '..', a drive, a non-DDS name and an empty slot are refused");
            a_check.Check(TextureCopyPathFor("textures\\effects\\a.dds") == "textures\\heartofmagic\\blackflame\\copies\\effects\\a.dds",
                "textures: the copy path is textures\\heartofmagic\\blackflame\\copies\\<path after textures\\>");
        }

        // One coloured texture in a_format through RecolourDds: grey, alpha and format and mips kept, the source untouched
        void CheckRecolour(Checker& a_check, const std::filesystem::path& a_dir, DXGI_FORMAT a_format, const char* a_name)
        {
            const auto source = a_dir / fmt::format("orange_{}.dds", a_name);
            const auto target = a_dir / "out" / fmt::format("orange_{}.dds", a_name);
            Mip0 before;
            if (!WriteDds(source, a_format, Fill::kOrange) || !ReadMip0(source, before)) {
                a_check.Check(false, fmt::format("textures: could not make the {} test texture", a_name));
                return;
            }
            const auto sourceBytes = Bytes(source);
            const auto result = RecolourDds(source, target, kTestTint);
            Mip0 after;
            const bool read = result.outcome == RecolourOutcome::kRecoloured && ReadMip0(target, after);
            const int expected = static_cast<int>(Luminance(kOrange[0] / 255.0f, kOrange[1] / 255.0f, kOrange[2] / 255.0f) *
                                                      kTestTint * 255.0f + 0.5f);
            std::string why = result.reason;
            const bool grey = read && GreyWithAlpha(after, before, expected, why);
            a_check.Check(grey && after.format == a_format && after.mips == kTestMips && result.bytes > 0,
                fmt::format("textures: a coloured {} becomes grey {} with alpha kept, the same format and {} mips ({}; format {}, mips {})",
                    a_name, expected, kTestMips, why.empty() ? "ok" : why, static_cast<int>(after.format), after.mips));
            a_check.Check(Bytes(source) == sourceBytes, fmt::format("textures: the {} source file is not changed", a_name));
        }

        void CheckSkipsAndFailures(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto grey = a_dir / "grey_bc1.dds";
            const auto greyOut = a_dir / "out" / "grey_bc1.dds";
            const bool made = WriteDds(grey, DXGI_FORMAT_BC1_UNORM, Fill::kGrey);
            const auto skipped = RecolourDds(grey, greyOut, kTestTint);
            a_check.Check(made && skipped.outcome == RecolourOutcome::kGreyscale && !std::filesystem::exists(greyOut),
                fmt::format("textures: a grey BC1 is skipped and nothing is written ({}, saturation {:.3f})", skipped.reason,
                    skipped.saturation));

            const auto broken = a_dir / "broken.dds";
            std::ofstream(broken, std::ios::binary) << std::string("DDS ") + std::string(20, '\x7F');
            const auto brokenOut = a_dir / "out" / "broken.dds";
            const auto failed = RecolourDds(broken, brokenOut, kTestTint);
            a_check.Check(failed.outcome == RecolourOutcome::kFailed && !failed.reason.empty() && !std::filesystem::exists(brokenOut),
                fmt::format("textures: a broken DDS fails cleanly with a reason and writes nothing ({})", failed.reason));

            const auto twoChannel = a_dir / "orange_bc5.dds";
            const bool madeBc5 = WriteDds(twoChannel, DXGI_FORMAT_BC5_UNORM, Fill::kOrange);
            const auto bc5 = RecolourDds(twoChannel, a_dir / "out" / "orange_bc5.dds", kTestTint);
            a_check.Check(madeBc5 && bc5.outcome == RecolourOutcome::kFailed && !std::filesystem::exists(a_dir / "out" / "orange_bc5.dds"),
                fmt::format("textures: a BC5 (no blue channel) is left with a reason ({})", bc5.reason));
        }

        // A NIF naming the test textures in every kind of slot the rule looks at (and some it must not touch)
        void CheckNifSlots(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto in = a_dir / "in";
            const bool made = WriteDds(in / "textures" / "effects" / "homtest_orange.dds", DXGI_FORMAT_BC3_UNORM, Fill::kOrange) &&
                              WriteDds(in / "textures" / "effects" / "homtest_grey.dds", DXGI_FORMAT_BC1_UNORM, Fill::kGrey) &&
                              WriteDds(in / "textures" / "effects" / "homtest_fx.dds", DXGI_FORMAT_BC1_UNORM, Fill::kOrange);
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            auto& hdr = nif.GetHeader();
            auto set = std::make_unique<nifly::BSShaderTextureSet>();
            set->textures[kSlotDiffuse].get() = "Textures/Effects/HoMTest_Orange.dds";
            set->textures[1].get() = "textures\\effects\\homtest_orange_n.dds";  // normal map: not a slot the rule takes
            set->textures[kSlotGlow].get() = "textures\\effects\\homtest_grey.dds";
            const std::uint32_t setId = hdr.AddBlock(std::move(set));
            for (int i = 0; i < 2; ++i) {  // two lighting shaders sharing the set: its slots once
                auto lit = std::make_unique<nifly::BSLightingShaderProperty>();
                lit->textureSetRef.index = setId;
                hdr.AddBlock(std::move(lit));
            }
            const auto addEffect = [&](const char* a_name, const char* a_texture, bool a_palette) {
                auto effect = std::make_unique<nifly::BSEffectShaderProperty>();
                effect->name.get() = a_name;
                effect->sourceTexture.get() = a_texture;
                effect->shaderFlags1 = a_palette ? (effect->shaderFlags1 | kSlsf1GreyscaleToPaletteColor) :
                                                   (effect->shaderFlags1 & ~kSlsf1GreyscaleToPaletteColor);
                hdr.AddBlock(std::move(effect));
            };
            addEffect("FxOff", "textures\\effects\\homtest_fx.dds", false);
            addEffect("FxPalette", "textures\\effects\\homtest_fx.dds", true);
            addEffect("FxMissing", "textures\\effects\\homtest_missing.dds", false);
            addEffect("FxOurs", kPalettePath, false);

            TextureJob job;
            job.inputs = { in };
            job.out = a_dir / "nifout";
            RetargetTextures(nif, job, "meshes\\test.nif");
            const auto nifFile = a_dir / "retarget.nif";
            SaveNif(nif, nifFile);
            nifly::NifFile back;
            LoadNif(back, nifFile);
            auto& backHdr = back.GetHeader();
            auto* backSet = backHdr.GetBlock<nifly::BSShaderTextureSet>(setId);  // NiStringVector has no const []
            std::map<std::string, std::string> effects;
            for (std::uint32_t id = 0; id < backHdr.GetNumBlocks(); ++id) {
                if (const auto* effect = backHdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                    effects[effect->name.get()] = effect->sourceTexture.get();
                }
            }
            const std::string orangeCopy = "textures\\heartofmagic\\blackflame\\copies\\effects\\homtest_orange.dds";
            const std::string fxCopy = "textures\\heartofmagic\\blackflame\\copies\\effects\\homtest_fx.dds";
            a_check.Check(made && backSet && backSet->textures[kSlotDiffuse].get() == orangeCopy &&
                              backSet->textures[1].get() == "textures\\effects\\homtest_orange_n.dds" &&
                              backSet->textures[kSlotGlow].get() == "textures\\effects\\homtest_grey.dds",
                "textures: NIF diffuse slot repointed at its grey copy, normal slot and a grey glow left (read back)");
            a_check.Check(effects["FxOff"] == fxCopy && effects["FxPalette"] == "textures\\effects\\homtest_fx.dds" &&
                              effects["FxMissing"] == "textures\\effects\\homtest_missing.dds" && effects["FxOurs"] == kPalettePath,
                "textures: effect source off the palette repointed; a palette shader's, a missing one and our own left");
            const auto& t = job.totals;
            a_check.Check(t.recoloured == 2 && t.greyscale == 1 && t.missing == 1 && t.failed == 0 && t.slots == 4 && t.repointed == 2 &&
                              std::filesystem::exists(job.out / orangeCopy) && std::filesystem::exists(job.out / fxCopy),
                fmt::format("textures: counts (recoloured {}, greyscale {}, missing {}, failed {}, slots {}, repointed {})", t.recoloured,
                    t.greyscale, t.missing, t.failed, t.slots, t.repointed));

            // A second NIF naming the same texture in another role: the copy made once is used
            nifly::NifFile second;
            second.Create(nifly::NiVersion::getSSE());
            auto effect = std::make_unique<nifly::BSEffectShaderProperty>();
            effect->sourceTexture.get() = "textures\\effects\\homtest_orange.dds";
            effect->shaderFlags1 &= ~kSlsf1GreyscaleToPaletteColor;
            const std::uint32_t effectId = second.GetHeader().AddBlock(std::move(effect));
            RetargetTextures(second, job, "meshes\\second.nif");
            const auto* again = second.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(effectId);
            a_check.Check(again && again->sourceTexture.get() == orangeCopy && job.totals.recoloured == 2 && job.totals.repointed == 3,
                "textures: a texture met again (another NIF, another role) reuses its copy, not made twice");
        }
    }

    void CheckTextures(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto dir = a_dir / "textures";
        std::filesystem::create_directories(dir);
        CheckPaths(a_check);
        CheckRecolour(a_check, dir, DXGI_FORMAT_BC3_UNORM, "BC3");
        CheckRecolour(a_check, dir, DXGI_FORMAT_BC7_UNORM, "BC7");
        CheckRecolour(a_check, dir, DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8");
        CheckSkipsAndFailures(a_check, dir);
        CheckNifSlots(a_check, dir);
    }
}
