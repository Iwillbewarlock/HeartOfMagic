#pragma once

// =============================================================================
// GreyGoldenTable - the golden test's released tables, one per kBuilderOutputVersion (GreyGolden.h)
// =============================================================================
// Each version's arrays and kTables row are pasted from --self-test --print-golden and never edited afterwards: a change
// of output gets a new kBuilderOutputVersion and a new table beside the old ones. A released version's pin (kReleasedPins,
// at the end) is written by hand, never pasted.

#include "GreyGolden.h"

#include <initializer_list>
#include <iterator>

namespace GreyBuild::Golden
{
    // ---- version 1: made 2026-10-18 on one machine (the first table). It is the build's gate already
    // (GreySelfTestGate.cmake); it has not been confirmed on a second machine yet. Regenerated in place, before version 1
    // was released: on 2026-10-18 for the sRGB source palette fixture (GreyGolden.h kPaletteSrgb, kKeyPaletteSrgb): two
    // files and two lines added; on 2026-10-19 for the four texel-test fixtures (GreyGolden.h kThresholds 8-11): the
    // textures NIF's copy (601d000000000001) and its progress line changed (two more texture deps), two texture copies
    // and their two dds lines added (minchannel_edge, lit_over: recoloured; sat_edge, lit_under: left grey, no copy).
    // Every other row and the constant digest unchanged
    // constants:
    //   kGreyscaleChroma=0x1.99999ap-5
    //   kLitFloor=0x1.99999ap-4
    //   kColouredChroma=0x1.333334p-3
    //   kColouredShare=0x1.47ae14p-8
    //   kTextureTintGlow=0x1p+0
    //   kTextureTintEffect=0x1p+0
    //   kTintPercent=100
    //   kTintOneEpsilon=0x1.47ae14p-8
    //   kSlsf2GlowMap=64
    //   kSlsf1OwnEmit=4194304
    //   kSlsf1GreyscaleToPaletteColor=16
    //   kSlsf1GreyscaleToPaletteAlpha=32
    //   kLumaRed=0x1.b367ap-3
    //   kLumaGreen=0x1.6e2eb2p-1
    //   kLumaBlue=0x1.27bb3p-4
    //   kStatLitWeight=0x1.999999999999ap-4
    //   kStatColouredSat=0x1p-2
    //   kStatMinChannel=0x1.47ae147ae147bp-6
    //   kStatColouredFrac=0x1.999999999999ap-3
    //   kStatMeanSat=0x1.3333333333333p-2
    //   kSimLitAlpha=0x1.999999999999ap-5
    //   kSimGrid=128
    //   kSrgbLinearLimit=0x1.4b5dccp-5
    //   kSrgbLinearScale=0x1.9d70a4p+3
    //   kSrgbOffset=0x1.c28f5cp-5
    //   kSrgbGamma=0x1.333334p+1
    //   kLinearLimit=0x1.9a5c38p-9
    //   kRiskWarmSat=0x1.6666666666666p-2
    //   kRiskMinChannel=0x1.47ae147ae147bp-6
    //   kRiskSoftDetail=0x1.eb851eb851eb8p-5
    //   kRiskHaloLumMax=0x1.47ae147ae147bp-7
    //   kRiskMaxKeySat=0x1.999999999999ap-5
    //   kRiskMaxVcSat=0x1.999999999999ap-4
    //   kRiskTexColoredFrac=0x1.999999999999ap-3
    //   kRiskTexMeanSat=0x1.3333333333333p-2
    //   kRiskSheetRatio=0x1p+2
    //   kRiskSheetCover=0x1p-1
    //   kRiskVanishRatio=0x1p-2
    //   kRiskMinSrcEnergy=0x1.47ae147ae147bp-7
    //   kRiskMinGBand=0x1.999999999999ap-5
    //   kRiskDarkLum=0x1.3333333333333p-3
    //   kRiskEnergyFloor=0x1.a36e2eb1c432dp-14
    //   kRiskModelSpread=0x1p+1
    //   kRiskDimRatio=0x1p-2
    //   kAlphaTestOn=512
    //   kBlendInvSrcAlpha=7
    //   kSlsf2VertexColors=32
    //   kUnusedKeyValue=-0x1.c363ccp+127
    //   kHugeFloatValue=0x1.93e594p+99
    //   kFloatVarEmissiveMultiple=0
    //   kFloatVarAlpha=5
    //   kKeyFormat=BFK4
    //   DIRECTX_TEX_VERSION=209
    //   directxtex port=2025-10-27
    //   nifly port=1.0.0 port 1
    inline constexpr GoldenFile kFilesV1[] = {
        { "meshes\\heartofmagic\\blackflame\\copies\\601d000000000001\\copy.nif", 2981, 0x16dff952e645981aull },
        { "meshes\\heartofmagic\\blackflame\\copies\\601d000000000002\\copy.nif", 351, 0x71f2ea55e22169eeull },
        { "meshes\\heartofmagic\\blackflame\\copies\\601d000000000003\\copy.nif", 1176, 0x9e279bab79dd71edull },
        { "meshes\\heartofmagic\\blackflame\\copies\\601d000000000004\\copy.nif", 2259, 0xc2258f3268cd5c8cull },
        { "meshes\\heartofmagic\\blackflame\\copies\\601d000000000006\\copy.nif", 697, 0xa36a9e8e9028df33ull },
        { "meshes\\heartofmagic\\blackflame\\copies\\601d000000000007\\copy.nif", 608, 0x8e5872452912ead3ull },
        { "textures\\heartofmagic\\blackflame\\copies\\00968a124d75c834\\copy.dds", 5616, 0xba44603ee46bdfa3ull },
        { "textures\\heartofmagic\\blackflame\\copies\\1a2585f0a5cff507\\copy.dds", 4224, 0x676620e88d1c2a62ull },
        { "textures\\heartofmagic\\blackflame\\copies\\2314c43fbafec4cc\\copy.dds", 5616, 0x77a3dd431277a76full },
        { "textures\\heartofmagic\\blackflame\\copies\\2b45044bed5f3a7f\\copy.dds", 384, 0x88c6cf549b6c2782ull },
        { "textures\\heartofmagic\\blackflame\\copies\\2d318c0133258b6b\\copy.dds", 292, 0xb44ee1b327549cb3ull },
        { "textures\\heartofmagic\\blackflame\\copies\\41d19d732bc6fb39\\copy.dds", 4224, 0xbb9428fe9de698afull },
        { "textures\\heartofmagic\\blackflame\\copies\\792a0e164c79d5d8\\copy.dds", 528, 0x8354a0f78fd7590aull },
        { "textures\\heartofmagic\\blackflame\\copies\\7d74829f8362922a\\palette.dds", 16512, 0x7611ee4bcc6a5079ull },
        { "textures\\heartofmagic\\blackflame\\copies\\8a42a7e91652d8b8\\copy.dds", 168, 0xfb7cf35f6fda8b91ull },
        { "textures\\heartofmagic\\blackflame\\copies\\95d90c23f3e69c62\\copy.dds", 2872, 0xc934bfb9c2b6fb82ull },
        { "textures\\heartofmagic\\blackflame\\copies\\a3278ff9b05297ce\\copy.dds", 5616, 0x77a3dd431277a76full },
        { "textures\\heartofmagic\\blackflame\\copies\\aac0eb7a5313f41b\\copy.dds", 5636, 0xdfaebdcd40dffc0aull },
        { "textures\\heartofmagic\\blackflame\\copies\\b30491fcec3e1aa1\\copy.dds", 21972, 0xc6197c51fcc575a6ull },
        { "textures\\heartofmagic\\blackflame\\copies\\b9ffb04893f937fc\\palette.dds", 660, 0xf26c2e84a2d05f8dull },
        { "textures\\heartofmagic\\blackflame\\copies\\bdce7db14964ec14\\palette.dds", 16512, 0x1946a11c03863b79ull },
        { "textures\\heartofmagic\\blackflame\\copies\\e55f2e4bb75bfdff\\copy.dds", 2872, 0xc934bfb9c2b6fb82ull },
        { "textures\\heartofmagic\\blackflame\\copies\\ef695c0bd6368ba8\\copy.dds", 5636, 0xb10b5705e9cc4244ull },
        { "textures\\heartofmagic\\blackflame\\copies\\fd78abc3b48b4db5\\copy.dds", 4224, 0x98565835cd45dafdull },
    };
    inline constexpr GoldenLine kLinesV1[] = {
        { "dds", "00968a124d75c834", "ok", "" },
        { "dds", "1a2585f0a5cff507", "ok", "" },
        { "dds", "2314c43fbafec4cc", "ok", "" },
        { "dds", "2b45044bed5f3a7f", "ok", "" },
        { "dds", "2d318c0133258b6b", "ok", "" },
        { "dds", "41d19d732bc6fb39", "ok", "" },
        { "dds", "792a0e164c79d5d8", "ok", "" },
        { "dds", "7d74829f8362922a", "ok", "" },
        { "dds", "8a42a7e91652d8b8", "ok", "" },
        { "dds", "95d90c23f3e69c62", "ok", "" },
        { "dds", "a3278ff9b05297ce", "ok", "" },
        { "dds", "aac0eb7a5313f41b", "ok", "" },
        { "dds", "b30491fcec3e1aa1", "ok", "" },
        { "dds", "b9ffb04893f937fc", "ok", "" },
        { "dds", "bdce7db14964ec14", "ok", "" },
        { "dds", "e55f2e4bb75bfdff", "ok", "" },
        { "dds", "ef695c0bd6368ba8", "ok", "" },
        { "dds", "fd78abc3b48b4db5", "ok", "" },
        { "mps", "601d000000000006", "ok", "addn:40012 textures\\heartofmagic\\blackflame\\copies\\b30491fcec3e1aa1\\copy.dds@c6197c51fcc575a6" },
        { "nif", "601d000000000001", "ok", "textures\\heartofmagic\\blackflame\\copies\\1a2585f0a5cff507\\copy.dds@676620e88d1c2a62 textures\\heartofmagic\\blackflame\\copies\\2b45044bed5f3a7f\\copy.dds@88c6cf549b6c2782 textures\\heartofmagic\\blackflame\\copies\\2d318c0133258b6b\\copy.dds@b44ee1b327549cb3 textures\\heartofmagic\\blackflame\\copies\\41d19d732bc6fb39\\copy.dds@bb9428fe9de698af textures\\heartofmagic\\blackflame\\copies\\792a0e164c79d5d8\\copy.dds@8354a0f78fd7590a textures\\heartofmagic\\blackflame\\copies\\8a42a7e91652d8b8\\copy.dds@fb7cf35f6fda8b91 textures\\heartofmagic\\blackflame\\copies\\a3278ff9b05297ce\\copy.dds@77a3dd431277a76f textures\\heartofmagic\\blackflame\\copies\\b30491fcec3e1aa1\\copy.dds@c6197c51fcc575a6 textures\\heartofmagic\\blackflame\\copies\\ef695c0bd6368ba8\\copy.dds@b10b5705e9cc4244 textures\\heartofmagic\\blackflame\\copies\\fd78abc3b48b4db5\\copy.dds@98565835cd45dafd" },
        { "nif", "601d000000000002", "fallback", "" },
        { "nif", "601d000000000003", "ok", "textures\\heartofmagic\\blackflame\\copies\\a3278ff9b05297ce\\copy.dds@77a3dd431277a76f textures\\heartofmagic\\blackflame\\copies\\aac0eb7a5313f41b\\copy.dds@dfaebdcd40dffc0a textures\\heartofmagic\\blackflame\\copies\\e55f2e4bb75bfdff\\copy.dds@c934bfb9c2b6fb82" },
        { "nif", "601d000000000004", "fallback", "addn:40012 textures\\heartofmagic\\blackflame\\copies\\2314c43fbafec4cc\\copy.dds@77a3dd431277a76f textures\\heartofmagic\\blackflame\\copies\\7d74829f8362922a\\palette.dds@7611ee4bcc6a5079 textures\\heartofmagic\\blackflame\\copies\\bdce7db14964ec14\\palette.dds@1946a11c03863b79" },
        { "nif", "601d000000000007", "ok", "textures\\heartofmagic\\blackflame\\copies\\b9ffb04893f937fc\\palette.dds@f26c2e84a2d05f8d" },
        { "txst", "601d000000000005", "ok", "textures\\heartofmagic\\blackflame\\copies\\00968a124d75c834\\copy.dds@ba44603ee46bdfa3 textures\\heartofmagic\\blackflame\\copies\\95d90c23f3e69c62\\copy.dds@c934bfb9c2b6fb82" },
    };

    inline constexpr GoldenTable kTables[] = {
        { 1, "595ab0076f8d450d", kFilesV1, std::size(kFilesV1), kLinesV1, std::size(kLinesV1) },
    };

    // ---- released pins: one row per version a tester package shipped with, never printed by --print-golden (so a pasted
    // table cannot overwrite one) and never removed. Version 1 is not pinned yet. Procedure (Make-TesterPackage.ps1 refuses
    // a builder whose kBuilderOutputVersion has no row here and prints the row to add): run --self-test, add the row its
    // "NOTE golden table v<n> fnv64 <hex>" line gives ({ <n>, "<hex>" },), commit, build Release again, package
    inline constexpr std::initializer_list<ReleasedPin> kReleasedPins = {
    };
}
