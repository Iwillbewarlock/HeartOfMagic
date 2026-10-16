#pragma once

// =============================================================================
// GreyShapes - every effect-shader shape of a NIF with what its look depends on (glow rules, visual-risk check)
// =============================================================================
// One reading of a NIF shared by the glow rules (GreyGlow.h) and the offline visual-risk check (GreyRisk.h): each shape
// (BSTriShape family, NiTriShape / NiTriStrips, NiParticleSystem / BSStripParticleSystem) whose shader is a
// BSEffectShaderProperty, with its blend (NiAlphaProperty), shader flags and textures, the colours its shader can show
// (base colour and every colour controller value and key, reached directly or through NiControllerSequence controlled
// blocks), the emissive multiple's largest value, and its vertex colours. The same rules as the prototype census
// (scratchpad glow-rule/census.js, nif2.js), so the check's counts can be compared with its report.

#include "GreyBuild.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nifly
{
    class NifFile;
}

namespace GreyBuild
{
    // NiAlphaProperty flags: bit 9 alpha test on; destination blend values (bits 5-8)
    inline constexpr std::uint16_t kAlphaTestOn = 1u << 9;
    inline constexpr std::uint16_t kBlendInvSrcAlpha = 7;
    // SLSF2 bit 5, Vertex_Colors (nif.xml SkyrimShaderPropertyFlags2; SLSF1 bit 5 palette alpha is in GreyBuild.h)
    inline constexpr std::uint32_t kSlsf2VertexColors = 1u << 5;
    // A key or interpolator value at or below this is the NIF's "unused" marker (-FLT_MAX), not a colour
    inline constexpr float kUnusedKeyValue = -3e38f;
    // A float controller value at or above this is no emissive multiple (FLT_MAX markers)
    inline constexpr float kHugeFloatValue = 1e30f;
    // BSEffectShaderPropertyFloatController controlled variable 0: the emissive multiple
    inline constexpr std::uint32_t kFloatVarEmissiveMultiple = 0;

    enum class ShapeBlend
    {
        kNoAlpha,   // no NiAlphaProperty
        kOpaque,    // blending off, no alpha test
        kTest,      // blending off, alpha test on
        kAdditive,  // blending on, destination ONE
        kNormal,    // blending on, destination INV_SRC_ALPHA
        kOther      // blending on, another destination
    };

    struct Rgb
    {
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
    };

    /// The keys a colour controller's NiPoint3Interpolator holds: the interpolator and its NiPosData (NIF_NPOS when none)
    struct ColourKeys
    {
        std::uint32_t controller = 0;
        std::uint32_t interpolator = 0;
        std::uint32_t data = 0;
        bool viaSequence = false;  // reached through an NiControllerSequence controlled block, not the controller itself
    };

    struct EffectShape
    {
        std::uint32_t block = 0;
        std::uint32_t shader = 0;
        std::string name;
        bool particles = false;
        bool hidden = false;     // its own or an ancestor node's flags bit 0
        bool billboard = false;  // under an NiBillboardNode
        ShapeBlend blend = ShapeBlend::kNoAlpha;
        std::uint32_t flags1 = 0;
        std::uint32_t flags2 = 0;
        std::string sourceTexture;
        std::string greyscaleTexture;
        Rgb baseColour;
        double colourScale = 1.0;   // baseColorScale
        std::vector<Rgb> colours;   // base colour, then every colour controller value and key reached
        int sequenceLinks = 0;      // controlled blocks that name one of its colour controllers
        double multipleMax = 1.0;   // baseColorScale and every emissive-multiple controller value and key
        std::vector<ColourKeys> colourKeys;
        int unreachedControllers = 0;  // colour controllers no NiPoint3Interpolator of theirs is reached for
        bool vertexColours = false;    // the geometry stores vertex colours
        Rgb vertexMean;                // alpha-weighted mean of the vertex colours (3 decimals, as the census)
        double vertexSaturation = 0.0; // of vertexMean (3 decimals)
        std::uint32_t vertexBlock = 0; // the block holding them: the BSTriShape, or the NiTriShapeData
    };

    [[nodiscard]] double Saturation(const Rgb& a_colour, double a_minChannel) noexcept;
    [[nodiscard]] double Luminance(const Rgb& a_colour) noexcept;
    [[nodiscard]] Rgb Multiply(const Rgb& a_left, const Rgb& a_right) noexcept;
    [[nodiscard]] double Round3(double a_value) noexcept;  // to 3 decimals, half up (the census's rounding)

    /// Palette shader by the census's rule: SLSF1 greyscale-to-palette-colour on and a source texture
    [[nodiscard]] bool IsPaletteShape(const EffectShape& a_shape) noexcept;

    /// Every effect-shader shape of a_nif, in block order
    [[nodiscard]] std::vector<EffectShape> EffectShapes(nifly::NifFile& a_nif);

    /// Every NiPoint3Interpolator a colour controller of a_shader reaches, directly or through NiControllerSequence
    /// controlled blocks (by controller reference, or for a block without one by node name, property type and controller
    /// type - the NIF's own way of naming it); plus a_unreached, its colour controllers with none
    [[nodiscard]] std::vector<ColourKeys> ColourKeysOf(nifly::NifFile& a_nif, std::uint32_t a_shader, int& a_unreached,
        int& a_sequenceLinks);
}
