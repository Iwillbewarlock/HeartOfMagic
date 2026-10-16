#pragma once

#include "Common.h"

#include <functional>
#include <string>

// =============================================================================
// RECOLOUR - grey palette and desaturated base colour on effect materials
// =============================================================================
// C6: what is left of the runtime recolour serves only the two keys kept until the copies are proven in game,
// hitArt (HitArt.cpp) and handArt (CasterArt.cpp), plus boundWeapon (PlayerArt.cpp, until the bound-weapon copies);
// all default off. This material code goes when those keys go. The read-only helpers below (ForEachEffectProperty,
// DescribeMaterials, HasGreyMaterial, HasColorController) stay: the self-test and the material checks use them.
//
// Game thread only. Every BSEffectShaderProperty under a root gets its own copy
// of its material (materials are pooled: never edit the shared one, other
// instances, NPC projectiles included, may use it) with the grey palette as its
// greyscale texture and its base colour RGB set to luminance * 0.6 (alpha kept).
// A shape without a palette flag only gets the desaturated base colour (no flag is
// added). A material whose greyscale texture or path is already one of our palettes
// is skipped, so calling it again on the same tree is cheap and changes nothing.
//
// Not touched: BSLightingShaderProperty (its emissive colour is a pointer that
// may be shared) and colour controllers (likely shared). The base colour is
// desaturated on every recoloured shape, but a controller animating the
// property's colour every frame may overwrite it; the stats count such
// properties.

namespace BlackFlame
{
    struct RecolorStats
    {
        int effectGeometries = 0;  // geometries with a BSEffectShaderProperty
        int paletteSwapped = 0;    // recoloured, the property maps grey to the palette
        int baseColorOnly = 0;     // recoloured, no palette flag: only the base colour changes
        int alreadyOurs = 0;       // skipped, already recoloured
        int colorControlled = 0;   // of those seen, properties with a BSEffectShaderPropertyColorController
        int paletteAlpha = 0;      // of those seen, properties mapping alpha through the palette too
        int engineCopied = 0;      // SetMaterial kept its own copy (ours freed)
        int keptOurs = 0;          // SetMaterial kept our copy
        int legacyGeometries = 0;  // NiGeometry (not BSGeometry) nodes with an effect property
        // no material or Create() null, the palette lost after SetMaterial, or a property already in
        // that terminal state
        int failed = 0;
        int masterSkipped = 0;     // effect geometries under a BSMasterParticleSystem: shared, left alone
        int addOnNodes = 0;        // "AddOnNode" nodes: they emit into a shared master particle system elsewhere

        [[nodiscard]] int Recolored() const noexcept { return paletteSwapped + baseColorOnly; }
        void Add(const RecolorStats& a_other) noexcept;
        [[nodiscard]] std::string Describe() const;
    };

    /// Game thread, at kPreLoadGame / kNewGame: forgets the properties that lost the palette
    void ResetRecolorState();

    /// A property's material values and palette flags before a recolour
    struct PropertyOriginal
    {
        RE::NiPointer<RE::NiSourceTexture> greyscaleTexture;
        RE::BSFixedString greyscaleTexturePath;
        RE::NiColorA baseColor;
        bool paletteColor = false;
        bool paletteAlpha = false;
    };

    /// Told about every property a recolour changed, with what it was before
    using RecolorObserver = std::function<void(RE::BSEffectShaderProperty*, const PropertyOriginal&)>;

    /// Recolours every effect material under a_root (null root: nothing).
    RecolorStats RecolorTree(RE::NiAVObject* a_root);
    RecolorStats RecolorTree(RE::NiAVObject* a_root, const RecolorObserver& a_observer);

    /// Puts a recoloured property back: a fresh copy of its current material with the original
    /// greyscale texture, path and base colour (SetMaterial(copy, true)), and its palette flags as
    /// they were (never on without a texture). Game thread only. False without a material.
    bool RestoreProperty(RE::BSEffectShaderProperty* a_prop, const PropertyOriginal& a_original);

    /// "prop=0x... mat=0x... grey=yes/no paletteFlag=yes/no" for the first few effect properties
    /// under a_root, and how many are grey (read only; any grey on an NPC instance = contamination)
    [[nodiscard]] std::string DescribeMaterials(RE::NiAVObject* a_root);

    /// (property, geometry or null for a legacy NiGeometry, legacy)
    using EffectPropertyVisitor = std::function<void(RE::BSEffectShaderProperty*, RE::BSGeometry*, bool)>;

    /// Every BSEffectShaderProperty under a_root: on BSGeometry (particle systems included) and on
    /// legacy NiGeometry, which BSVisit::TraverseScenegraphGeometries skips (read only by itself)
    void ForEachEffectProperty(RE::NiAVObject* a_root, const EffectPropertyVisitor& a_visit);

    /// The property has a BSEffectShaderPropertyColorController (it rewrites the colour per frame)
    [[nodiscard]] bool HasColorController(const RE::BSEffectShaderProperty* a_prop);

    /// The property's material carries our palette (texture pointer or path)
    [[nodiscard]] bool HasGreyMaterial(RE::BSEffectShaderProperty* a_prop);
}
