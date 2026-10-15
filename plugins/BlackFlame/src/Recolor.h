#pragma once

#include "Common.h"

#include <string>

// =============================================================================
// RECOLOUR - grey palette and desaturated base colour on effect materials
// =============================================================================
// Game thread only. Every BSEffectShaderProperty under a root gets its own copy
// of its material (never the shared one: other instances, NPC projectiles
// included, may use the same material) with the grey palette as its greyscale
// texture and its base colour RGB set to luminance * config tintScale (alpha
// kept). A property without the grey-to-palette colour flag gets the flag too
// when config forcePalette is on, so its own texture's brightness is mapped
// through the grey ramp (replacers such as Embers XD drop the flag on flame
// bodies); with it off only the base colour changes on such a shape. A material whose greyscale texture is already the palette is skipped,
// so calling it again on the same tree is cheap and changes nothing.
//
// Not touched: BSLightingShaderProperty (its emissive colour is a pointer that
// may be shared) and colour controllers (likely shared). A controller animating
// the property's colour every frame overrides the base colour change; the stats
// count such properties. Particle colour modifiers only with config
// particleColors (ParticleColors.h).

namespace BlackFlame
{
    struct RecolorStats
    {
        int effectGeometries = 0;  // geometries with a BSEffectShaderProperty
        int paletteSwapped = 0;    // recoloured, the property maps grey to the palette
        int forcedPalette = 0;     // recoloured, palette colour flag added (config forcePalette)
        int baseColorOnly = 0;     // recoloured, no palette flag: only the base colour changes
        int alreadyOurs = 0;       // skipped, already recoloured
        int colorControlled = 0;   // of those seen, properties with a BSEffectShaderPropertyColorController
        int engineCopied = 0;      // SetMaterial kept its own copy (ours freed)
        int keptOurs = 0;          // SetMaterial kept our copy
        int legacyGeometries = 0;  // NiGeometry (not BSGeometry) nodes with an effect property
        int failed = 0;            // material missing or Create() returned null
        int colorModifiers = 0;    // BSPSysSimpleColorModifiers desaturated (config particleColors)

        [[nodiscard]] int Recolored() const noexcept { return paletteSwapped + forcedPalette + baseColorOnly; }
        void Add(const RecolorStats& a_other) noexcept;
        [[nodiscard]] std::string Describe() const;
    };

    /// Recolours every effect material under a_root (null root: nothing).
    RecolorStats RecolorTree(RE::NiAVObject* a_root);

    /// Sets a light's diffuse colour to config lightColor. False for a null light.
    bool TintLight(RE::NiLight* a_light);

    /// "prop=0x... mat=0x..." for the first few effect properties under a_root (read only)
    [[nodiscard]] std::string DescribeMaterials(RE::NiAVObject* a_root);
}
