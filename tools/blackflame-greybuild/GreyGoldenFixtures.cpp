// =============================================================================
// GreyGoldenFixtures - --make-golden-fixtures <dir>: the golden test's input files, written once (GreyGolden.h)
// =============================================================================
// Run once to make <dir>\input; the files are then committed (tools\blackflame-greybuild\golden\input) and the test
// only reads them, so a change to the library under test cannot quietly change its own inputs. Textures: the self-test's
// 64 x 64 orange / grey ones with a full mip chain (WriteTestTexture) in BC7, sRGB BC7, BC3, BC1, B8G8R8A8 and BC5, a
// 16 x 4 BC7 down to 1 x 1, small B8G8R8A8 ones on either side of each colour threshold or exactly on a texel test (exact
// 8-bit values), and a B8G8R8A8_UNORM_SRGB source palette (exact stored bytes, no conversion).
// NIFs: one lighting diffuse and effect shaders on every texture, a two-channel source, glow shapes, the self-test's
// palette source (MakeSourceNif), an MPS with AddOnNode 9 and an additive palette shader on the sRGB palette.
// The sRGB palette and its NIF were added to version 1's fixtures before that version was released (2026-10-18): they
// were written by this code into a fresh folder and copied in, every other input file kept as committed. The same way,
// on 2026-10-19 and still before the release: the four texel-test fixtures (sat_edge, minchannel_edge, lit_under,
// lit_over) and textures.nif, which names them.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "GreyGolden.h"
#include "GreySelfTest.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

#include <DirectXTex.h>
#include <NifFile.hpp>
#include <Shaders.hpp>

#include <array>
#include <fmt/format.h>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        using Texel = std::array<std::uint8_t, 4>;  // r, g, b, a
        using TexelAt = std::function<Texel(std::size_t, std::size_t)>;

        constexpr std::uint8_t kOpaque = 255;
        constexpr Texel kMidGrey{ 128, 128, 128, kOpaque };
        constexpr std::size_t kSmallSide = 8;       // the chroma pair
        constexpr std::size_t kShareSide = 32;      // 1024 texels: the share and fraction pairs
        constexpr std::size_t kMeanSatSide = 10;    // 100 texels: the mean saturation pair
        constexpr std::size_t kWideWidth = 16;      // the non-square BC7: 16 x 4, mips 8 x 2, 4 x 1, 2 x 1, 1 x 1
        constexpr std::size_t kWideHeight = 4;
        constexpr Texel kChromaUnder{ 140, 128, 128, kOpaque };  // red over grey: 12/255 = 0.047 < kGreyscaleChroma,
        constexpr Texel kChromaOver{ 141, 128, 128, kOpaque };   // 13/255 = 0.051
        constexpr std::size_t kShareUnder = 5;      // texels at chroma 51/255 = 0.2: 5/1024 <= kColouredShare < 6/1024
        constexpr std::size_t kShareOver = 6;
        constexpr Texel kShareTexel{ 179, 128, 128, kOpaque };
        constexpr std::size_t kFracUnder = 204;     // dark texels at S 12/40 = 0.3 (chroma 0.047): 204/1024 rounds to
        constexpr std::size_t kFracOver = 205;      // 0.199 < kStatColouredFrac, 205/1024 to 0.200
        constexpr Texel kFracTexel{ 40, 28, 28, kOpaque };
        constexpr std::size_t kMeanSatUnder = 52;   // of 100 unlit texels (max 25/255 < 0.1): this many (25, 18, 18), the
        constexpr std::size_t kMeanSatOver = 51;    // rest (25, 17, 17): mean S 0.2992 -> 0.299, 0.2996 -> 0.300
        constexpr Texel kMeanSatHigh{ 25, 18, 18, kOpaque };
        constexpr Texel kMeanSatLow{ 25, 17, 17, kOpaque };
        constexpr Texel kSatEdgeTexel{ 40, 30, 30, kOpaque };   // (max - min) / max = 10 / 40, exactly kStatColouredSat
        constexpr std::size_t kMinChannelWidth = 10;            // 10 x 1: one kMinChannelHigh, nine kMinChannelLow:
        constexpr Texel kMinChannelHigh{ 6, 0, 0, kOpaque };    // mean red 51 / 2550 = 0.02 = kStatMinChannel exactly
        constexpr Texel kMinChannelLow{ 5, 0, 0, kOpaque };
        constexpr std::size_t kLitEdgeCount = 5;                // 5 of 1024: within kColouredShare in RecolourDds's own measure
        constexpr Texel kLitUnder{ 197, 100, 100, 33 };         // alpha x max 6501: not lit (ComputeStats)
        constexpr Texel kLitOver{ 241, 120, 120, 27 };          // 6507: lit
        constexpr Texel kFaintGrey{ 128, 128, 128, 40 };        // 5120: never lit for ComputeStats, lit and visible for the chroma
        constexpr std::uint16_t kAlphaNormal = 0x12ED;    // blending on, SRC_ALPHA -> INV_SRC_ALPHA, alpha test
        constexpr std::uint16_t kAlphaAdditive = 0x000D;  // blending on, SRC_ALPHA -> ONE
        constexpr int kMpsAddon = 9;
        constexpr std::size_t kSrgbPaletteWidth = 32;   // the sRGB source palette: an orange ramp along u, alpha down u and v
        constexpr std::size_t kSrgbPaletteHeight = 4;
        const nifly::Color4 kWarm{ 1.0f, 0.5f, 0.2f, 1.0f };
        const nifly::Vector3 kOrangeEmissive{ 1.0f, 0.5f, 0.0f };

        void Fail(const std::string& a_what)
        {
            throw std::runtime_error(fmt::format("fixture: {}", a_what));
        }

        /// a_width x a_height texels from a_at, in a_format, with a full mip chain when a_mips (else one mip)
        void WriteTexels(const std::filesystem::path& a_file, std::size_t a_width, std::size_t a_height, DXGI_FORMAT a_format,
            const TexelAt& a_at, bool a_mips)
        {
            DirectX::ScratchImage base;
            if (FAILED(base.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, a_width, a_height, 1, 1))) {
                Fail(PathText(a_file) + ": no image");
            }
            const DirectX::Image& image = *base.GetImage(0, 0, 0);
            for (std::size_t y = 0; y < a_height; ++y) {
                for (std::size_t x = 0; x < a_width; ++x) {
                    const Texel texel = a_at(x, y);
                    std::copy(texel.begin(), texel.end(), image.pixels + y * image.rowPitch + x * 4);
                }
            }
            const auto filter = static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_FORCE_NON_WIC);
            DirectX::ScratchImage mips;
            if (a_mips && FAILED(DirectX::GenerateMipMaps(image, filter, 0, mips))) {
                Fail(PathText(a_file) + ": no mips");
            }
            const DirectX::ScratchImage& chain = a_mips ? mips : base;
            DirectX::ScratchImage encoded;
            const HRESULT hr = DirectX::IsCompressed(a_format) ?
                                   DirectX::Compress(chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), a_format,
                                       static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_BC7_QUICK),
                                       DirectX::TEX_THRESHOLD_DEFAULT, encoded) :
                                   DirectX::Convert(chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), a_format, filter,
                                       DirectX::TEX_THRESHOLD_DEFAULT, encoded);
            std::filesystem::create_directories(a_file.parent_path());
            if (FAILED(hr) || FAILED(DirectX::SaveToDDSFile(encoded.GetImages(), encoded.GetImageCount(), encoded.GetMetadata(),
                                  DirectX::DDS_FLAGS_NONE, a_file.c_str()))) {
                Fail(PathText(a_file) + ": not written");
            }
        }

        /// The first a_count texels in row order are a_some, the rest a_rest
        TexelAt Counted(std::size_t a_width, std::size_t a_count, Texel a_some, Texel a_rest)
        {
            return [=](std::size_t a_x, std::size_t a_y) { return a_y * a_width + a_x < a_count ? a_some : a_rest; };
        }

        /// One mip of B8G8R8A8_UNORM_SRGB whose stored bytes are a_at's values as given (no Convert: that would apply
        /// the sRGB curve to them)
        void WriteStoredSrgb(const std::filesystem::path& a_file, std::size_t a_width, std::size_t a_height, const TexelAt& a_at)
        {
            DirectX::ScratchImage image;
            if (FAILED(image.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, a_width, a_height, 1, 1))) {
                Fail(PathText(a_file) + ": no image");
            }
            const DirectX::Image& target = *image.GetImage(0, 0, 0);
            for (std::size_t y = 0; y < a_height; ++y) {
                for (std::size_t x = 0; x < a_width; ++x) {
                    const Texel texel = a_at(x, y);
                    std::uint8_t* to = target.pixels + y * target.rowPitch + x * 4;
                    to[0] = texel[2];  // B, G, R, A
                    to[1] = texel[1];
                    to[2] = texel[0];
                    to[3] = texel[3];
                }
            }
            std::filesystem::create_directories(a_file.parent_path());
            if (FAILED(DirectX::SaveToDDSFile(target, DirectX::DDS_FLAGS_NONE, a_file.c_str()))) {
                Fail(PathText(a_file) + ": not written");
            }
        }

        void Test(const std::filesystem::path& a_file, unsigned a_format, bool a_orange)
        {
            if (!SelfTest::WriteTestTexture(a_file, a_format, a_orange)) {
                Fail(PathText(a_file) + ": not written");
            }
        }

        void WriteTextures(const std::filesystem::path& a_in)
        {
            Test(a_in / Golden::kBc7, DXGI_FORMAT_BC7_UNORM, true);
            Test(a_in / Golden::kBc7Srgb, DXGI_FORMAT_BC7_UNORM_SRGB, true);
            Test(a_in / Golden::kBc3, DXGI_FORMAT_BC3_UNORM, true);
            Test(a_in / Golden::kBc1Glow, DXGI_FORMAT_BC1_UNORM, true);
            Test(a_in / Golden::kBgra, DXGI_FORMAT_B8G8R8A8_UNORM, true);
            Test(a_in / Golden::kGreyBc1, DXGI_FORMAT_BC1_UNORM, false);
            Test(a_in / Golden::kBc5, DXGI_FORMAT_BC5_UNORM, true);
            Test(a_in / Golden::kDecal, DXGI_FORMAT_BC3_UNORM, true);
            Test(a_in / Golden::kDecalGlow, DXGI_FORMAT_BC1_UNORM, true);
            Test(a_in / Golden::kFxFire01, DXGI_FORMAT_BC3_UNORM, true);
            Test(a_in / Golden::kFxFire02, DXGI_FORMAT_B8G8R8A8_UNORM, false);
            Test(a_in / Golden::kGradFlame, DXGI_FORMAT_B8G8R8A8_UNORM, true);
            Test(a_in / std::string(BlackFlame::DataPathRule::kOurPalette), DXGI_FORMAT_B8G8R8A8_UNORM, true);
            Test(a_in / std::string(BlackFlame::DataPathRule::kOurPaletteAdd), DXGI_FORMAT_B8G8R8A8_UNORM, false);
            WriteTexels(a_in / Golden::kBc7Wide, kWideWidth, kWideHeight, DXGI_FORMAT_BC7_UNORM,
                [](std::size_t a_x, std::size_t a_y) {
                    return Texel{ 255, static_cast<std::uint8_t>(64 + 12 * a_x), static_cast<std::uint8_t>(40 * a_y),
                        static_cast<std::uint8_t>(255 - 50 * a_y) };
                },
                true);
            const auto bgra = DXGI_FORMAT_B8G8R8A8_UNORM;
            const auto& t = Golden::kThresholds;
            WriteTexels(a_in / t[0].path, kSmallSide, kSmallSide, bgra, Counted(kSmallSide, 0, kMidGrey, kChromaUnder), false);
            WriteTexels(a_in / t[1].path, kSmallSide, kSmallSide, bgra, Counted(kSmallSide, 0, kMidGrey, kChromaOver), false);
            WriteTexels(a_in / t[2].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kShareUnder, kShareTexel, kMidGrey), false);
            WriteTexels(a_in / t[3].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kShareOver, kShareTexel, kMidGrey), false);
            WriteTexels(a_in / t[4].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kFracUnder, kFracTexel, kMidGrey), false);
            WriteTexels(a_in / t[5].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kFracOver, kFracTexel, kMidGrey), false);
            WriteTexels(a_in / t[6].path, kMeanSatSide, kMeanSatSide, bgra, Counted(kMeanSatSide, kMeanSatUnder, kMeanSatHigh, kMeanSatLow), false);
            WriteTexels(a_in / t[7].path, kMeanSatSide, kMeanSatSide, bgra, Counted(kMeanSatSide, kMeanSatOver, kMeanSatHigh, kMeanSatLow), false);
            WriteTexels(a_in / t[8].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kFracOver, kSatEdgeTexel, kMidGrey), false);
            WriteTexels(a_in / t[9].path, kMinChannelWidth, 1, bgra, Counted(kMinChannelWidth, 1, kMinChannelHigh, kMinChannelLow), false);
            WriteTexels(a_in / t[10].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kLitEdgeCount, kLitUnder, kFaintGrey), false);
            WriteTexels(a_in / t[11].path, kShareSide, kShareSide, bgra, Counted(kShareSide, kLitEdgeCount, kLitOver, kFaintGrey), false);
            WriteStoredSrgb(a_in / Golden::kPaletteSrgb, kSrgbPaletteWidth, kSrgbPaletteHeight, [](std::size_t a_x, std::size_t a_y) {
                return Texel{ 255, static_cast<std::uint8_t>(64 + 6 * a_x), static_cast<std::uint8_t>(16 + 2 * a_x),
                    static_cast<std::uint8_t>(255 - 6 * a_x - 10 * a_y) };
            });
        }

        template <class T>
        T* Add(nifly::NifFile& a_nif, std::unique_ptr<T> a_block, std::uint32_t& a_id)
        {
            T* raw = a_block.get();
            a_id = a_nif.GetHeader().AddBlock(std::move(a_block));
            return raw;
        }

        nifly::NifFile NewNif()
        {
            nifly::NifFile nif;
            nif.Create(nifly::NiVersion::getSSE());
            return nif;
        }

        /// An effect shader off the palette on a_texture ("" none), its id
        std::uint32_t AddEffect(nifly::NifFile& a_nif, const std::string& a_name, const char* a_texture, const nifly::Color4& a_base)
        {
            std::uint32_t id = 0;
            auto* effect = Add(a_nif, std::make_unique<nifly::BSEffectShaderProperty>(), id);
            effect->name.get() = a_name;
            effect->shaderFlags1 &= ~kSlsf1GreyscaleToPaletteColor;
            effect->baseColor = a_base;
            effect->sourceTexture.get() = a_texture;
            return id;
        }

        /// A lighting shader on a texture set (slot 0 a_diffuse, slot 2 a_glow), a glow map shader when a_glow is set
        void AddLighting(nifly::NifFile& a_nif, const char* a_diffuse, const char* a_glow)
        {
            auto set = std::make_unique<nifly::BSShaderTextureSet>();
            set->textures[kSlotDiffuse].get() = a_diffuse;
            set->textures[kSlotGlow].get() = a_glow;
            const auto setId = a_nif.GetHeader().AddBlock(std::move(set));
            std::uint32_t id = 0;
            auto* lit = Add(a_nif, std::make_unique<nifly::BSLightingShaderProperty>(), id);
            lit->textureSetRef.index = setId;
            lit->emissiveColor = kOrangeEmissive;
            lit->shaderFlags1 &= ~kSlsf1OwnEmit;
            if (a_glow[0] != '\0') {
                lit->SetShaderType(nifly::BSLSP_GLOWMAP);
            }
        }

        void AddShape(nifly::NifFile& a_nif, std::uint32_t a_shader, std::uint16_t a_alphaFlags)
        {
            std::uint32_t alphaId = 0;
            Add(a_nif, std::make_unique<nifly::NiAlphaProperty>(), alphaId)->flags = a_alphaFlags;
            std::uint32_t shapeId = 0;
            auto* shape = Add(a_nif, std::make_unique<nifly::BSTriShape>(), shapeId);
            shape->ShaderPropertyRef()->index = a_shader;
            shape->AlphaPropertyRef()->index = alphaId;
            a_nif.GetRootNode()->childRefs.AddBlockRef(shapeId);
        }

        void Save(nifly::NifFile& a_nif, const std::filesystem::path& a_file)
        {
            std::filesystem::create_directories(a_file.parent_path());
            SaveNif(a_nif, a_file);
        }

        void WriteNifs(const std::filesystem::path& a_in)
        {
            auto textures = NewNif();
            AddLighting(textures, Golden::kBc7, "");
            int n = 0;
            for (const char* path : { Golden::kBc3, Golden::kBgra, Golden::kBc7Wide, Golden::kGreyBc1 }) {
                (void)AddEffect(textures, fmt::format("Effect{}", n++), path, kWarm);
            }
            for (const auto& threshold : Golden::kThresholds) {
                (void)AddEffect(textures, fmt::format("Effect{}", n++), threshold.path, kWarm);
            }
            Save(textures, a_in / Golden::kTexturesNif);

            auto bc5 = NewNif();
            (void)AddEffect(bc5, "TwoChannel", Golden::kBc5, kWarm);
            Save(bc5, a_in / Golden::kBc5Nif);

            // an additive soft warm glow with no texture beside a textured normal-blend shape (G1), and a glow map shader
            auto glow = NewNif();
            AddShape(glow, AddEffect(glow, "SoftGlow", "", kWarm), kAlphaAdditive);
            AddShape(glow, AddEffect(glow, "Body", Golden::kBc3, kWarm), kAlphaNormal);
            AddLighting(glow, Golden::kBc7Srgb, Golden::kBc1Glow);
            Save(glow, a_in / Golden::kGlowNif);

            SelfTest::MakeSourceNif(a_in / Golden::kPaletteNif);

            auto mps = NewNif();
            auto node = std::make_unique<nifly::BSValueNode>();
            node->name.get() = fmt::format("AddOnNode{}", kMpsAddon);
            node->value = kMpsAddon;
            mps.GetRootNode()->childRefs.AddBlockRef(mps.GetHeader().AddBlock(std::move(node)));
            AddShape(mps, AddEffect(mps, "Embers", Golden::kBgra, kWarm), kAlphaAdditive);
            Save(mps, a_in / Golden::kMpsNif);

            // an additive palette shader on the sRGB source palette (its source texture already grey: no texture copy)
            auto srgb = NewNif();
            const auto paletteId = AddEffect(srgb, "FlameSrgb", Golden::kFxFire02, kWarm);
            auto* palette = srgb.GetHeader().GetBlock<nifly::BSEffectShaderProperty>(paletteId);
            palette->shaderFlags1 |= kSlsf1GreyscaleToPaletteColor;
            palette->greyscaleTexture.get() = Golden::kPaletteSrgb;
            AddShape(srgb, paletteId, kAlphaAdditive);
            Save(srgb, a_in / Golden::kPaletteSrgbNif);
        }
    }

    int MakeGoldenFixtures(const std::filesystem::path& a_dir)
    {
        const auto in = a_dir / Golden::kInputFolder;
        std::error_code ec;
        if (std::filesystem::exists(in, ec)) {
            std::cerr << fmt::format("{} exists: the committed fixtures are never made again in place (a new version's "
                                     "fixtures go to a new folder)\n", PathText(in));
            return kExitFail;
        }
        try {
            WriteTextures(in);
            WriteNifs(in);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return kExitFail;
        }
        std::cout << fmt::format("wrote the golden fixtures to {}\n", PathText(in));
        return kExitOk;
    }
}
