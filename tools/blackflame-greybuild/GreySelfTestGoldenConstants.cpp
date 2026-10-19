// =============================================================================
// GreySelfTestGoldenConstants - the golden test's constant digest (GreyGolden.h ConstantDigest)
// =============================================================================
// Moved out of GreySelfTestGolden.cpp (contract 6, which added the effect shader palettes' fire test constants).

#include "GreyGolden.h"
#include "GreyKeyed.h"
#include "GreyRisk.h"
#include "GreyShapes.h"
#include "GreyTexStats.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/CopyContractKeys.h"

#include <DirectXTex.h>

#include <fmt/format.h>
#include <type_traits>

namespace GreyBuild::Golden
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;

        template <class T>
        void Put(std::string& a_out, const char* a_name, T a_value)
        {
            if constexpr (std::is_floating_point_v<T>) {
                a_out += fmt::format("{}={:a}\n", a_name, a_value);
            } else {
                a_out += fmt::format("{}={}\n", a_name, a_value);
            }
        }
    }

    Constants ConstantDigest()
    {
        std::string t;
        Put(t, "kGreyscaleChroma", kGreyscaleChroma);
        Put(t, "kLitFloor", kLitFloor);
        Put(t, "kColouredChroma", kColouredChroma);
        Put(t, "kColouredShare", kColouredShare);
        Put(t, "kTextureTintGlow", kTextureTintGlow);
        Put(t, "kTextureTintEffect", kTextureTintEffect);
        Put(t, "kTintPercent", kTintPercent);
        Put(t, "kTintOneEpsilon", kTintOneEpsilon);
        Put(t, "kSlsf2GlowMap", kSlsf2GlowMap);
        Put(t, "kSlsf1OwnEmit", kSlsf1OwnEmit);
        Put(t, "kSlsf1GreyscaleToPaletteColor", kSlsf1GreyscaleToPaletteColor);
        Put(t, "kSlsf1GreyscaleToPaletteAlpha", kSlsf1GreyscaleToPaletteAlpha);
        Put(t, "kLumaRed", kLumaRed);
        Put(t, "kLumaGreen", kLumaGreen);
        Put(t, "kLumaBlue", kLumaBlue);
        Put(t, "kStatLitWeight", kStatLitWeight);
        Put(t, "kStatColouredSat", kStatColouredSat);
        Put(t, "kStatMinChannel", kStatMinChannel);
        Put(t, "kStatColouredFrac", kStatColouredFrac);
        Put(t, "kStatMeanSat", kStatMeanSat);
        Put(t, "kSimLitAlpha", kSimLitAlpha);
        Put(t, "kSimGrid", kSimGrid);
        Put(t, "kSrgbLinearLimit", kSrgbLinearLimit);
        Put(t, "kSrgbLinearScale", kSrgbLinearScale);
        Put(t, "kSrgbOffset", kSrgbOffset);
        Put(t, "kSrgbGamma", kSrgbGamma);
        Put(t, "kLinearLimit", kLinearLimit);
        Put(t, "kRiskWarmSat", kRiskWarmSat);
        Put(t, "kRiskMinChannel", kRiskMinChannel);
        Put(t, "kRiskSoftDetail", kRiskSoftDetail);
        Put(t, "kRiskHaloLumMax", kRiskHaloLumMax);
        Put(t, "kRiskMaxKeySat", kRiskMaxKeySat);
        Put(t, "kRiskMaxVcSat", kRiskMaxVcSat);
        Put(t, "kRiskTexColoredFrac", kRiskTexColoredFrac);
        Put(t, "kRiskTexMeanSat", kRiskTexMeanSat);
        Put(t, "kRiskSheetRatio", kRiskSheetRatio);
        Put(t, "kRiskSheetCover", kRiskSheetCover);
        Put(t, "kRiskVanishRatio", kRiskVanishRatio);
        Put(t, "kRiskMinSrcEnergy", kRiskMinSrcEnergy);
        Put(t, "kRiskMinGBand", kRiskMinGBand);
        Put(t, "kRiskDarkLum", kRiskDarkLum);
        Put(t, "kRiskEnergyFloor", kRiskEnergyFloor);
        Put(t, "kRiskModelSpread", kRiskModelSpread);
        Put(t, "kRiskDimRatio", kRiskDimRatio);
        Put(t, "kAlphaTestOn", kAlphaTestOn);
        Put(t, "kBlendInvSrcAlpha", kBlendInvSrcAlpha);
        Put(t, "kSlsf2VertexColors", kSlsf2VertexColors);
        Put(t, "kUnusedKeyValue", kUnusedKeyValue);
        Put(t, "kHugeFloatValue", kHugeFloatValue);
        Put(t, "kFloatVarEmissiveMultiple", kFloatVarEmissiveMultiple);
        Put(t, "kFloatVarAlpha", kFloatVarAlpha);
        Put(t, "kEfshFireHueMax", kEfshFireHueMax);
        Put(t, "kEfshFireHueWrap", kEfshFireHueWrap);
        Put(t, "kEfshFireMinSat", kEfshFireMinSat);
        Put(t, "kEfshFireMinValue", kEfshFireMinValue);
        Put(t, "kEfshFireShare", kEfshFireShare);
        Put(t, "kKeyFormat", CC::kKeyFormat);
        Put(t, "DIRECTX_TEX_VERSION", DIRECTX_TEX_VERSION);
        Put(t, "directxtex port", BLACKFLAME_DIRECTXTEX_PORT);
        Put(t, "nifly port", BLACKFLAME_NIFLY_PORT);
        return { t, CC::Hex16(CC::Fnv64(t)) };
    }
}
