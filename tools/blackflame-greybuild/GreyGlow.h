#pragma once

// =============================================================================
// GreyGlow - the colour rules of the full-grey copies after GreyEffects (README "Glow rules")
// =============================================================================
// GreyEffects greys every effect shader's base colour and the keys of colour controllers it reaches directly. These
// rules read the source NIF (the same block numbers: the builder changes values only) and write the copy's colours from
// the source values, so nothing is greyed twice:
//   G  each shader without the palette flag by its shapes (glow-rule.md section 3): an additive soft glow (no texture, or
//      texture detail under kRiskSoftDetail) whose source colour is warm (S >= kRiskWarmSat, texture mean and vertex
//      colours included) goes to RGB 0 (G1) unless that leaves the NIF nothing drawn (then G2); an additive one with
//      detail x tintAdd (G2); an additive soft neutral one and normal blend x tint (G3, G5); not drawn: x tint (G0);
//   P  a palette shader (derived mode, GreyPalette.h) takes the palette derived from its source palette, and its base colour
//      and keys keep their red as grey (r, r, r): red picks the palette row, so the derived palette's rows are read as
//      the source's were;
//   K  every colour key a shader's colour controllers reach - directly, or through NiControllerSequence controlled blocks
//      (by reference, or by node name / property type / controller type) - gets the shader's own colour rule;
//   V  vertex colours of a shape without the palette flag go to their luminance, alpha kept (a palette shape's vertex red
//      selects its palette row and stays).

#include "GreyPalette.h"
#include "GreyShapes.h"

#include <set>
#include <string>
#include <vector>

namespace nifly
{
    class NifFile;
}

namespace GreyBuild
{
    struct GlowTotals
    {
        int g1 = 0;                // G1: shaders whose colours went to RGB 0 (additive soft warm glows)
        int g1Kept = 0;            // NIFs where G1 would have left nothing drawn: G2 instead
        int g2 = 0;                // G2: shaders without the palette flag at luminance x tintAdd
        int g3 = 0;                // G3: shapes, additive soft neutral (luminance x tint)
        int g5 = 0;                // G5: shapes, normal blend (luminance x tint)
        int unread = 0;            // shapes whose texture could not be read (G2)
        int mixed = 0;             // Y18: shaders shared by shapes of different classes (no G1)
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
        PaletteJob* palettes = nullptr;  // none: palette shaders keep GreyEffects' palette (the godskin rule)
        float tint = kDefaultTint;
        float tintAdd = kDefaultTintAdd;
        GlowTotals totals;
        std::set<std::string> haloKept;  // Y7: NIFs (source paths) where G1 would have emptied the effect: kept at G2
    };

    /// The rules above on a_copy (GreyEffects already ran on it) from a_source; prints a NOTE per thing it could not do
    void ApplyGlowRules(nifly::NifFile& a_copy, nifly::NifFile& a_source, GlowContext& a_ctx, const std::string& a_rel);

    /// "glow  ..." for a run's end
    [[nodiscard]] std::string GlowTotalsLine(const GlowTotals& a_totals);
}
