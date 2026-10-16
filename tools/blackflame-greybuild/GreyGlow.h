#pragma once

// =============================================================================
// GreyGlow - the colour rules of the full-grey copies after GreyEffects (README "Glow rules")
// =============================================================================
// GreyEffects greys every effect shader's base colour and the keys of colour controllers it reaches directly. These
// rules read the source NIF (the same block numbers: the builder changes values only) and write the copy's colours from
// the source values, so nothing is greyed twice:
//   K  every colour key a shader's colour controllers reach - directly, or through NiControllerSequence controlled blocks
//      (by reference, or by node name / property type / controller type) - gets the shader's own colour rule;
//   V  vertex colours of a shape without the palette flag go to their luminance, alpha kept (a palette shape's vertex red
//      selects its palette row and stays).

#include "GreyShapes.h"

#include <string>
#include <vector>

namespace nifly
{
    class NifFile;
}

namespace GreyBuild
{
    class TextureSource;

    struct GlowTotals
    {
        int sequenceKeys = 0;      // K: colour keys reached only through controlled blocks, greyed
        int blendValues = 0;       // K: a colour controller's own blend interpolator value, greyed
        int unreached = 0;         // colour controllers without a reachable NiPoint3Interpolator (no keys to grey)
        int vertexShapes = 0;      // V: shapes whose vertex colours went to luminance
        int vertexShared = 0;      // V: geometry a palette shape shares, left
    };

    /// A run's glow rules: where textures are judged from, the tints, what they did
    struct GlowContext
    {
        TextureSource* textures = nullptr;
        float tint = kDefaultTint;
        float tintAdd = kDefaultTintAdd;
        GlowTotals totals;
    };

    /// The rules above on a_copy (GreyEffects already ran on it) from a_source; prints a NOTE per thing it could not do
    void ApplyGlowRules(nifly::NifFile& a_copy, nifly::NifFile& a_source, GlowContext& a_ctx, const std::string& a_rel);

    /// "glow  ..." for a run's end
    [[nodiscard]] std::string GlowTotalsLine(const GlowTotals& a_totals);
}
