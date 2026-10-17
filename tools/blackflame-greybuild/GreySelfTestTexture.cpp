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

#include <cmath>
#include <cstdlib>
#include <fmt/format.h>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

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
        constexpr std::uint8_t kOpaque = 255;
        constexpr std::size_t kSpeckEvery = 32;   // one 4x4 orange speck (one BC block) per 32x32 cell: 1/64 of the texels
                                                  // (the old all-visible-pixels saturation, about 0.016, called it grey)
        constexpr std::size_t kSpeckSide = 4;
        constexpr std::size_t kDarkGreyTop = 60;  // the dark grey runs 0..this across each row
        constexpr float kFloatTolerance = 1e-5f;
        const nifly::Vector3 kOrangeEmissive{ 1.0f, 0.5f, 0.0f };

        enum class Fill
        {
            kOrange,    // orange, alpha a gradient
            kGrey,      // mid grey, alpha a gradient
            kSpecks,    // black with small orange specks, opaque (T2)
            kDarkGrey,  // a dark grey ramp, opaque (T2: 5:6:5 noise)
            kSmoke,     // mid grey with small orange specks, opaque (U4: a low mean chroma that shows orange)
            kHidden     // orange with alpha 0 everywhere (U5: an additive texture that ignores its alpha)
        };

        [[nodiscard]] std::uint8_t TestAlpha(std::size_t a_x) noexcept
        {
            return static_cast<std::uint8_t>(kAlphaFloor + (255 - kAlphaFloor) * a_x / (kTestSize - 1));
        }

        void FillTexel(std::uint8_t* a_texel, std::size_t a_x, std::size_t a_y, Fill a_fill)
        {
            const bool speck = a_x % kSpeckEvery < kSpeckSide && a_y % kSpeckEvery < kSpeckSide;
            const auto dark = static_cast<std::uint8_t>(kDarkGreyTop * a_x / (kTestSize - 1));
            for (int c = 0; c < 3; ++c) {
                switch (a_fill) {
                case Fill::kOrange: a_texel[c] = kOrange[c]; break;
                case Fill::kGrey: a_texel[c] = kGrey; break;
                case Fill::kSpecks: a_texel[c] = speck ? kOrange[c] : 0; break;
                case Fill::kSmoke: a_texel[c] = speck ? kOrange[c] : kGrey; break;
                case Fill::kHidden: a_texel[c] = kOrange[c]; break;
                default: a_texel[c] = dark; break;
                }
            }
            a_texel[3] = a_fill == Fill::kOrange || a_fill == Fill::kGrey ? TestAlpha(a_x) : a_fill == Fill::kHidden ? 0 : kOpaque;
        }

        // A kTestSize square with a full mip chain, filled as a_fill says, written in a_format
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
                    FillTexel(row + x * 4, x, y, a_fill);
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


        // A legacy A8L8 DDS (DDPF_LUMINANCE | DDPF_ALPHAPIXELS, 16 bits): without luminance expansion DirectXTex reads it as
        // R8G8 (two channels), with it as grey RGBA (T5)
        void WriteLegacyA8L8(const std::filesystem::path& a_file)
        {
            constexpr std::uint32_t kSide = 4;
            constexpr std::uint32_t kHeaderSize = 124;
            constexpr std::uint32_t kFlags = 0x100F;          // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
            constexpr std::uint32_t kPixelFormatSize = 32;
            constexpr std::uint32_t kLuminanceAlpha = 0x20001;  // DDPF_LUMINANCE | DDPF_ALPHAPIXELS
            constexpr std::uint32_t kBits = 16;
            constexpr std::uint32_t kLuminanceMask = 0x00FF;
            constexpr std::uint32_t kAlphaMask = 0xFF00;
            constexpr std::uint32_t kCapsTexture = 0x1000;
            constexpr int kReserved = 11;
            constexpr char kLuminance = static_cast<char>(200);
            constexpr char kOpaqueAlpha = static_cast<char>(kOpaque);
            std::string bytes = "DDS ";
            const auto put = [&](std::uint32_t a_value) {
                for (int i = 0; i < 4; ++i) {
                    bytes.push_back(static_cast<char>((a_value >> (8 * i)) & 0xFF));
                }
            };
            for (const std::uint32_t value : { kHeaderSize, kFlags, kSide, kSide, kSide * 2, 0u, 0u }) {
                put(value);
            }
            for (int i = 0; i < kReserved; ++i) {
                put(0);
            }
            for (const std::uint32_t value : { kPixelFormatSize, kLuminanceAlpha, 0u, kBits, kLuminanceMask, 0u, 0u, kAlphaMask,
                     kCapsTexture, 0u, 0u, 0u, 0u }) {
                put(value);
            }
            for (std::uint32_t i = 0; i < kSide * kSide; ++i) {
                bytes.push_back(kLuminance);
                bytes.push_back(kOpaqueAlpha);
            }
            std::ofstream(a_file, std::ios::binary) << bytes;
        }

        void CheckSkipsAndFailures(Checker& a_check, const std::filesystem::path& a_dir)
        {
            a_check.Check(TextureCopyPathFor("textures\\effects\\a.dds", 1.0f) ==
                                  "textures\\heartofmagic\\blackflame\\copies\\_tint\\100\\effects\\a.dds" &&
                              TextureCopyPathFor("textures\\effects\\a.dds", 0.6f) ==
                                  "textures\\heartofmagic\\blackflame\\copies\\_tint\\60\\effects\\a.dds",
                "textures: the copy path is copies\\_tint\\<percent>\\<path after textures\\>, every tint in its own folder");
            // Second review: a source under textures\t60\ at tint 1 once got the same copy as textures\a.dds at tint 0.6
            const auto sourceT60 = TextureCopyPathFor("textures\\t60\\a.dds", 1.0f);
            const auto tinted = TextureCopyPathFor("textures\\a.dds", 0.6f);
            const auto sourceTint = TextureCopyPathFor("textures\\_tint\\60\\a.dds", 1.0f);
            a_check.Check(sourceT60 != tinted && sourceTint != tinted && sourceT60 != sourceTint,
                fmt::format("textures: no two sources share a copy path (t60\\a.dds at 1: {}, a.dds at 0.6: {}, _tint\\60\\a.dds at 1: {})",
                    sourceT60, tinted, sourceTint));

            const auto grey = a_dir / "grey_bc1.dds";
            const auto greyOut = a_dir / "out" / "grey_bc1.dds";
            const bool made = WriteDds(grey, DXGI_FORMAT_BC1_UNORM, Fill::kGrey);
            const auto skipped = RecolourDds(grey, greyOut, 1.0f);
            a_check.Check(made && skipped.outcome == RecolourOutcome::kGreyscale && !std::filesystem::exists(greyOut),
                fmt::format("textures: a grey BC1 at tint 1 is skipped and nothing is written ({})", skipped.reason));
            // U7: at another tint a grey texture is darkened into a copy (the diffuse carries --tint)
            const auto greyTinted = a_dir / "out" / "grey_bc1_t60.dds";
            const auto greyTintResult = RecolourDds(grey, greyTinted, kTestTint);
            a_check.Check(made && greyTintResult.outcome == RecolourOutcome::kRecoloured && std::filesystem::exists(greyTinted),
                fmt::format("textures: a grey BC1 at tint {} gets a darkened copy ({})", kTestTint,
                    greyTintResult.reason.empty() ? std::string("written") : greyTintResult.reason));

            // T2: small orange specks on black are judged by the specks; a dark grey with 5:6:5 noise stays grey
            const auto specks = a_dir / "specks_bc1.dds";
            const bool madeSpecks = WriteDds(specks, DXGI_FORMAT_BC1_UNORM, Fill::kSpecks);
            const auto specksResult = RecolourDds(specks, a_dir / "out" / "specks_bc1.dds", kTestTint);
            a_check.Check(madeSpecks && specksResult.outcome == RecolourOutcome::kRecoloured,
                fmt::format("textures: a black BC1 with small orange specks is recoloured (chroma {:.3f}{})", specksResult.chroma,
                    specksResult.reason.empty() ? std::string() : "; " + specksResult.reason));
            // U4: mid grey smoke with 1/64 orange specks: mean chroma about 0.016, but 1.6% of the lit pixels are orange
            const auto smoke = a_dir / "smoke_bc1.dds";
            const bool madeSmoke = WriteDds(smoke, DXGI_FORMAT_BC1_UNORM, Fill::kSmoke);
            const auto smokeResult = RecolourDds(smoke, a_dir / "out" / "smoke_bc1.dds", kTestTint);
            a_check.Check(madeSmoke && smokeResult.outcome == RecolourOutcome::kRecoloured,
                fmt::format("textures: mid grey with 1/64 orange specks is recoloured (mean chroma {:.3f}, coloured {:.2f}%{})",
                    smokeResult.chroma, smokeResult.colouredShare * 100.0f,
                    smokeResult.reason.empty() ? std::string() : "; " + smokeResult.reason));
            // U5: orange under alpha 0 everywhere (additive): measured over the lit pixels
            const auto hidden = a_dir / "hidden_bgra.dds";
            const bool madeHidden = WriteDds(hidden, DXGI_FORMAT_B8G8R8A8_UNORM, Fill::kHidden);
            const auto hiddenResult = RecolourDds(hidden, a_dir / "out" / "hidden_bgra.dds", kTestTint);
            a_check.Check(madeHidden && hiddenResult.outcome == RecolourOutcome::kRecoloured && hiddenResult.alphaIgnored,
                fmt::format("textures: orange under alpha 0 everywhere is measured ignoring alpha and recoloured (chroma {:.3f}{})",
                    hiddenResult.chroma, hiddenResult.reason.empty() ? std::string() : "; " + hiddenResult.reason));
            const auto dark = a_dir / "darkgrey_bc1.dds";
            const bool madeDark = WriteDds(dark, DXGI_FORMAT_BC1_UNORM, Fill::kDarkGrey);
            const auto darkResult = RecolourDds(dark, a_dir / "out" / "darkgrey_bc1.dds", 1.0f);  // the greyscale judgement (at another tint a grey texture is darkened, U7)
            a_check.Check(madeDark && darkResult.outcome == RecolourOutcome::kGreyscale,
                fmt::format("textures: a dark grey BC1 (5:6:5 noise) is skipped as grey ({})", darkResult.reason));

            // T5: a legacy A8L8 reads as grey, not as a two-channel failure
            const auto luminance = a_dir / "legacy_a8l8.dds";
            WriteLegacyA8L8(luminance);
            const auto luminanceResult = RecolourDds(luminance, a_dir / "out" / "legacy_a8l8.dds", 1.0f);
            a_check.Check(luminanceResult.outcome == RecolourOutcome::kGreyscale,
                fmt::format("textures: a legacy A8L8 DDS is read expanded and skipped as grey ({}, {})", luminanceResult.format,
                    luminanceResult.reason));

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

            // T17: a float format is refused, not clamped
            const auto floating = a_dir / "orange_f16.dds";
            const bool madeFloat = WriteDds(floating, DXGI_FORMAT_R16G16B16A16_FLOAT, Fill::kOrange);
            const auto floatResult = RecolourDds(floating, a_dir / "out" / "orange_f16.dds", kTestTint);
            a_check.Check(madeFloat && floatResult.outcome == RecolourOutcome::kFailed,
                fmt::format("textures: a float texture is refused ({})", floatResult.reason));
        }

        nifly::BSLightingShaderProperty* AddLighting(nifly::NifFile& a_nif, std::uint32_t a_set, bool a_glowType, bool a_glowFlag)
        {
            auto lit = std::make_unique<nifly::BSLightingShaderProperty>();
            lit->textureSetRef.index = a_set;
            lit->emissiveColor = kOrangeEmissive;
            lit->shaderFlags1 &= ~kSlsf1OwnEmit;  // nifly's default flags carry Own_Emit (U6 sets it where a case wants it)
            if (a_glowType) {
                lit->SetShaderType(nifly::BSLSP_GLOWMAP);
            }
            if (a_glowFlag) {
                lit->shaderFlags2 |= kSlsf2GlowMap;
            }
            auto* raw = lit.get();
            a_nif.GetHeader().AddBlock(std::move(lit));
            return raw;
        }

        std::uint32_t AddSet(nifly::NifFile& a_nif, const char* a_diffuse, const char* a_normal, const char* a_slot2)
        {
            auto set = std::make_unique<nifly::BSShaderTextureSet>();
            set->textures[kSlotDiffuse].get() = a_diffuse;
            set->textures[1].get() = a_normal;
            set->textures[kSlotGlow].get() = a_slot2;
            return a_nif.GetHeader().AddBlock(std::move(set));
        }

        void AddEffect(nifly::NifFile& a_nif, const char* a_name, const char* a_texture, bool a_palette)
        {
            auto effect = std::make_unique<nifly::BSEffectShaderProperty>();
            effect->name.get() = a_name;
            effect->sourceTexture.get() = a_texture;
            effect->shaderFlags1 = a_palette ? (effect->shaderFlags1 | kSlsf1GreyscaleToPaletteColor) :
                                               (effect->shaderFlags1 & ~kSlsf1GreyscaleToPaletteColor);
            a_nif.GetHeader().AddBlock(std::move(effect));
        }

        [[nodiscard]] bool EmissiveGrey(const nifly::BSLightingShaderProperty& a_shader)
        {
            const float grey = Luminance(kOrangeEmissive.x, kOrangeEmissive.y, kOrangeEmissive.z);
            const auto& c = a_shader.emissiveColor;
            return std::fabs(c.x - grey) < kFloatTolerance && std::fabs(c.y - grey) < kFloatTolerance &&
                   std::fabs(c.z - grey) < kFloatTolerance;
        }

        // A NIF naming the test textures in every kind of slot the rule looks at (and some it must not touch)
        void CheckNifSlots(Checker& a_check, const std::filesystem::path& a_dir)
        {
            const auto in = a_dir / "in";
            const auto effects = in / "textures" / "effects";
            const bool made = WriteDds(effects / "homtest_orange.dds", DXGI_FORMAT_BC3_UNORM, Fill::kOrange) &&
                              WriteDds(effects / "homtest_grey.dds", DXGI_FORMAT_BC1_UNORM, Fill::kGrey) &&
                              WriteDds(effects / "homtest_glow.dds", DXGI_FORMAT_BC1_UNORM, Fill::kOrange) &&
                              WriteDds(effects / "homtest_skin.dds", DXGI_FORMAT_BC1_UNORM, Fill::kOrange) &&
                              WriteDds(effects / "homtest_fx.dds", DXGI_FORMAT_BC1_UNORM, Fill::kOrange);
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            // Set A on a glow-type shader and on a shader with only the glow flag (shared: its slots once); set B on a
            // default shader, whose slot 2 is no glow map (T6)
            const std::uint32_t setA = AddSet(nif, "Textures/Effects/HoMTest_Orange.dds", "textures\\effects\\homtest_orange_n.dds",
                "textures\\effects\\homtest_glow.dds");
            const std::uint32_t setB = AddSet(nif, "textures\\effects\\homtest_grey.dds", "", "textures\\effects\\homtest_skin.dds");
            AddLighting(nif, setA, true, false);
            AddLighting(nif, setA, false, true);
            AddLighting(nif, setB, false, false);
            AddLighting(nif, setB, false, false)->shaderFlags1 |= kSlsf1OwnEmit;  // U6: emissive without a glow map
            AddEffect(nif, "FxOff", "textures\\effects\\homtest_fx.dds", false);
            AddEffect(nif, "FxPalette", "textures\\effects\\homtest_fx.dds", true);
            AddEffect(nif, "FxMissing", "textures\\effects\\homtest_missing.dds", false);
            AddEffect(nif, "FxOurs", kPalettePath, false);

            TextureJob job;
            job.inputs = { in };
            job.out = a_dir / "nifout";
            job.diffuseTint = kTestTint;
            RetargetTextures(nif, job, "meshes\\test.nif");
            const auto nifFile = a_dir / "retarget.nif";
            SaveNif(nif, nifFile);
            nifly::NifFile back;
            LoadNif(back, nifFile);
            auto& hdr = back.GetHeader();
            auto* backA = hdr.GetBlock<nifly::BSShaderTextureSet>(setA);  // NiStringVector has no const []
            auto* backB = hdr.GetBlock<nifly::BSShaderTextureSet>(setB);
            std::map<std::string, std::string> fx;
            std::vector<const nifly::BSLightingShaderProperty*> lit;
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* effect = hdr.GetBlock<nifly::BSEffectShaderProperty>(id)) {
                    fx[effect->name.get()] = effect->sourceTexture.get();
                } else if (const auto* shader = hdr.GetBlock<nifly::BSLightingShaderProperty>(id)) {
                    lit.push_back(shader);
                }
            }
            const std::string copies = "textures\\heartofmagic\\blackflame\\copies\\";
            const std::string orangeDiffuse = copies + "_tint\\60\\effects\\homtest_orange.dds";
            const std::string glowCopy = copies + "_tint\\100\\effects\\homtest_glow.dds";
            const std::string fxCopy = copies + "_tint\\100\\effects\\homtest_fx.dds";
            a_check.Check(made && backA && backA->textures[kSlotDiffuse].get() == orangeDiffuse &&
                              backA->textures[1].get() == "textures\\effects\\homtest_orange_n.dds" &&
                              backA->textures[kSlotGlow].get() == glowCopy,
                "textures: glow shader set - diffuse copy at tint 0.6 (_tint\\60\\), glow copy at tint 1 (_tint\\100\\), normal slot left (read back)");
            const std::string greyDiffuse = copies + "_tint\\60\\effects\\homtest_grey.dds";
            a_check.Check(backB && backB->textures[kSlotDiffuse].get() == greyDiffuse &&
                              backB->textures[kSlotGlow].get() == "textures\\effects\\homtest_skin.dds",
                "textures: default shader set - grey diffuse darkened at tint 0.6 (U7), slot 2 (not a glow map there) left though "
                "it is orange");
            a_check.Check(lit.size() == 4 && EmissiveGrey(*lit[0]) && EmissiveGrey(*lit[1]) && !EmissiveGrey(*lit[2]) &&
                              EmissiveGrey(*lit[3]),
                "textures: emissive colour greyed on the glow-type, the glow-flag and the Own_Emit shader, left on the default "
                "one (T3, U6)");
            a_check.Check(fx["FxOff"] == fxCopy && fx["FxPalette"] == "textures\\effects\\homtest_fx.dds" &&
                              fx["FxMissing"] == "textures\\effects\\homtest_missing.dds" && fx["FxOurs"] == kPalettePath,
                "textures: effect source off the palette repointed; a palette shader's, a missing one and our own left");
            const auto& t = job.totals;
            a_check.Check(t.recoloured == 4 && t.greyscale == 0 && t.missing == 1 && t.failed == 0 && t.slots == 5 && t.repointed == 4 &&
                              t.emissiveGreyed == 3 && std::filesystem::exists(job.out / orangeDiffuse) &&
                              std::filesystem::exists(job.out / greyDiffuse) &&
                              std::filesystem::exists(job.out / glowCopy) && std::filesystem::exists(job.out / fxCopy) &&
                              !std::filesystem::exists(job.out / copies / "_tint" / "100" / "effects" / "homtest_skin.dds") &&
                              !std::filesystem::exists(job.out / copies / "_tint" / "60" / "effects" / "homtest_skin.dds"),
                fmt::format("textures: counts (recoloured {}, greyscale {}, missing {}, failed {}, slots {}, repointed {}, emissive {})",
                    t.recoloured, t.greyscale, t.missing, t.failed, t.slots, t.repointed, t.emissiveGreyed));

            // A second NIF: the orange texture as an effect source (tint 1: a second copy) and again as a diffuse (reused)
            nifly::NifFile second;
            second.Create(nifly::NiVersion::getSSE());
            AddEffect(second, "Again", "textures\\effects\\homtest_orange.dds", false);
            const std::uint32_t setC = AddSet(second, "textures\\effects\\homtest_orange.dds", "", "");
            AddLighting(second, setC, false, false);
            // U8: the missing texture again, now as a diffuse (tint 0.6): judged missing once per path
            const std::uint32_t setD = AddSet(second, "textures\\effects\\homtest_missing.dds", "", "");
            AddLighting(second, setD, false, false);
            RetargetTextures(second, job, "meshes\\second.nif");
            a_check.Check(job.totals.missing == 1 && job.totals.slots == 8,
                fmt::format("textures: a missing texture met at a second tint counts once (missing {}, slots {}) (U8)",
                    job.totals.missing, job.totals.slots));
            std::string againSource;
            for (std::uint32_t id = 0; id < second.GetHeader().GetNumBlocks(); ++id) {
                if (const auto* effect = second.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(id)) {
                    againSource = effect->sourceTexture.get();
                }
            }
            auto* setAgain = second.GetHeader().GetBlock<nifly::BSShaderTextureSet>(setC);
            a_check.Check(againSource == copies + "_tint\\100\\effects\\homtest_orange.dds" && setAgain &&
                              setAgain->textures[kSlotDiffuse].get() == orangeDiffuse && job.totals.recoloured == 5 &&
                              job.totals.repointed == 6,
                fmt::format("textures: one texture at two tints gets two copies, each made once (recoloured {}, repointed {}) (T4)",
                    job.totals.recoloured, job.totals.repointed));
        }
    }

    bool WriteTestTexture(const std::filesystem::path& a_file, unsigned a_dxgiFormat, bool a_orange)
    {
        return WriteDds(a_file, static_cast<DXGI_FORMAT>(a_dxgiFormat), a_orange ? Fill::kOrange : Fill::kGrey);
    }

    void CheckTextures(Checker& a_check, const std::filesystem::path& a_dir)
    {
        const auto dir = a_dir / "textures";
        std::filesystem::create_directories(dir);
        CheckRecolour(a_check, dir, DXGI_FORMAT_BC3_UNORM, "BC3");
        CheckRecolour(a_check, dir, DXGI_FORMAT_BC7_UNORM, "BC7");
        CheckRecolour(a_check, dir, DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8");
        CheckSkipsAndFailures(a_check, dir);
        CheckNifSlots(a_check, dir);
    }
}
