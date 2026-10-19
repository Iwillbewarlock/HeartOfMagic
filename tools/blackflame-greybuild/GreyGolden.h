#pragma once

// =============================================================================
// GreyGolden - the golden-output test's fixture (names, objects) and its released tables' shape
// =============================================================================
// The fixture's input files are committed under tools\blackflame-greybuild\golden\input (laid out as the DLL's dump:
// meshes\..., textures\...), written once by --make-golden-fixtures (GreyGoldenFixtures.cpp), never made again at test
// time. GreySelfTestGolden.cpp writes the request around them, runs the key mode and compares every output file and
// every progress line with the table of kBuilderOutputVersion (GreyGoldenTable.h). A released table is never edited: a
// new version adds its own.

#include <cstddef>
#include <cstdint>
#include <string>

namespace GreyBuild::Golden
{
    inline constexpr const char* kInputFolder = "input";

    // The NIFs, Data-relative
    inline constexpr const char* kTexturesNif = "meshes\\golden\\textures.nif";  // lighting diffuse, effect sources
    inline constexpr const char* kBc5Nif = "meshes\\golden\\bc5.nif";            // a two-channel source: fallback
    inline constexpr const char* kGlowNif = "meshes\\golden\\glow.nif";          // glow rules: additive warm glow, glow map
    inline constexpr const char* kPaletteNif = "meshes\\golden\\palette.nif";    // the self-test source: palette shaders
    inline constexpr const char* kMpsNif = "meshes\\golden\\mps.nif";            // an MPS with AddOnNode 9
    inline constexpr const char* kPaletteSrgbNif = "meshes\\golden\\palettesrgb.nif";  // an additive palette shader, sRGB palette

    // The textures, Data-relative
    inline constexpr const char* kBc7 = "textures\\golden\\bc7.dds";
    inline constexpr const char* kBc7Srgb = "textures\\golden\\bc7srgb.dds";
    inline constexpr const char* kBc3 = "textures\\golden\\bc3.dds";
    inline constexpr const char* kBc1Glow = "textures\\golden\\bc1glow.dds";
    inline constexpr const char* kBgra = "textures\\golden\\bgra.dds";
    inline constexpr const char* kBc7Wide = "textures\\golden\\bc7wide.dds";  // 16 x 4, 5 mips down to 1 x 1
    inline constexpr const char* kGreyBc1 = "textures\\golden\\grey.dds";     // already grey: left at tint 1
    inline constexpr const char* kBc5 = "textures\\golden\\bc5.dds";
    inline constexpr const char* kDecal = "textures\\golden\\decal.dds";          // the TXST's TX00
    inline constexpr const char* kDecalGlow = "textures\\golden\\decalglow.dds";  // the TXST's TX03
    inline constexpr const char* kFxFire01 = "textures\\effects\\fxfire01.dds";   // the self-test source's names
    inline constexpr const char* kFxFire02 = "textures\\effects\\fxfire02.dds";
    inline constexpr const char* kGradFlame = "textures\\effects\\gradients\\gradflame01.dds";
    // A source palette stored sRGB (B8G8R8A8_UNORM_SRGB, exact bytes) on an additive palette shader: the derived palette's
    // sRGB branch (GreyPalette.cpp Desaturate: decoded to linear, luminance, encoded back)
    inline constexpr const char* kPaletteSrgb = "textures\\golden\\palettesrgb.dds";

    /// A texture one 8-bit step (or one texel) to either side of a threshold of the recolour decision at tint 1
    /// (RecolourDds: kGreyscaleChroma, kColouredShare; IsColouredTexture: kStatColouredFrac, kStatMeanSat), or exactly on
    /// one of ComputeStats's texel tests (kStatColouredSat, kStatMinChannel) and either side of its lit test
    /// (kStatLitWeight: alpha x max 6501 / 6507 against 0.1 x 255 x 255 = 6502.5); uncompressed B8G8R8A8, so the values are
    /// exact
    struct ThresholdFixture
    {
        const char* path;
        const char* rule;
        bool recoloured;  // over the line: a copy is made; under it: left as grey (no copy, no dep)
    };

    inline constexpr ThresholdFixture kThresholds[] = {
        { "textures\\golden\\chroma_under.dds", "kGreyscaleChroma 0.05: mean chroma 12/255", false },
        { "textures\\golden\\chroma_over.dds", "kGreyscaleChroma 0.05: mean chroma 13/255", true },
        { "textures\\golden\\share_under.dds", "kColouredShare 0.005: 5 of 1024 lit texels at chroma 0.2", false },
        { "textures\\golden\\share_over.dds", "kColouredShare 0.005: 6 of 1024 lit texels at chroma 0.2", true },
        { "textures\\golden\\frac_under.dds", "kStatColouredFrac 0.20: 204 of 1024 lit texels at S 0.3 (0.199)", false },
        { "textures\\golden\\frac_over.dds", "kStatColouredFrac 0.20: 205 of 1024 lit texels at S 0.3 (0.200)", true },
        { "textures\\golden\\meansat_under.dds", "kStatMeanSat 0.30: unlit mean (25, 17.52, 17.52) (0.299)", false },
        { "textures\\golden\\meansat_over.dds", "kStatMeanSat 0.30: unlit mean (25, 17.51, 17.51) (0.300)", true },
        { "textures\\golden\\sat_edge.dds", "kStatColouredSat 0.25: 205 of 1024 lit texels at S exactly 0.25, (40, 30, 30): not coloured", false },
        { "textures\\golden\\minchannel_edge.dds", "kStatMinChannel 0.02: 10 unlit texels, one (6, 0, 0) and nine (5, 0, 0): mean red "
                                                   "exactly 0.02, so mean S 1", true },
        { "textures\\golden\\lit_under.dds", "kStatLitWeight 0.1: 5 coloured texels at alpha x max 6501 (197, 100, 100, a 33): unlit", false },
        { "textures\\golden\\lit_over.dds", "kStatLitWeight 0.1: 5 coloured texels at alpha x max 6507 (241, 120, 120, a 27): lit", true },
    };

    // The request's objects (any 16 hex digits: the DLL's keys; the builder never checks them against their sources)
    inline constexpr std::uint64_t kKeyTextures = 0x601D000000000001ull;  // forms[0] model
    inline constexpr std::uint64_t kKeyBc5 = 0x601D000000000002ull;       // forms[0] muzzle
    inline constexpr std::uint64_t kKeyGlow = 0x601D000000000003ull;      // forms[1] model
    inline constexpr std::uint64_t kKeyPalette = 0x601D000000000004ull;   // forms[2] model
    inline constexpr std::uint64_t kKeyTxst = 0x601D000000000005ull;      // forms[3], txst[0]
    inline constexpr std::uint64_t kKeyMps = 0x601D000000000006ull;       // addnMps[0], ADDN 9's slot
    inline constexpr std::uint64_t kKeyPaletteSrgb = 0x601D000000000007ull;  // forms[4] model

    struct GoldenFile
    {
        const char* path;  // staging-relative
        std::uint64_t bytes;
        std::uint64_t fnv64;
    };

    struct GoldenLine
    {
        const char* kind;
        const char* key;      // key16
        const char* outcome;
        const char* deps;     // sorted, space-separated: <path>@<fnv64 hex> or addn:<index>
    };

    struct GoldenTable
    {
        std::uint32_t version;   // the kBuilderOutputVersion it was made with
        const char* constants;   // the constant digest (ConstantDigest) of that build
        const GoldenFile* files;
        std::size_t fileCount;
        const GoldenLine* lines;
        std::size_t lineCount;
    };

    /// The output-affecting constants the headers expose, as hex-float text, and its FNV-1a 64 (GreySelfTestGoldenConstants.cpp)
    struct Constants
    {
        std::string text;
        std::string digest;
    };

    [[nodiscard]] Constants ConstantDigest();

    /// A released version's pin (GreyGoldenTable.h kReleasedPins): the fnv64 (16 lower-case hex digits) of its table's
    /// text (GreySelfTestGolden.cpp TableText). Kept apart from the tables, which --print-golden prints, so pasting a
    /// printed table never overwrites a pin: a pinned version whose table is missing or whose text no longer hashes to its
    /// pin fails the self-test, so a released table cannot be regenerated in place
    struct ReleasedPin
    {
        std::uint32_t version;
        const char* fnv64;
    };
}
