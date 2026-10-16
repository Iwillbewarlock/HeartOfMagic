#pragma once

#include "Common.h"

// =============================================================================
// PALETTE - the grey gradient the recolour layers put on effect materials
// =============================================================================
// Loaded once on the game thread at kDataLoaded and kept alive for the session.

namespace BlackFlame
{
    /// Tries the palette path with and without the "textures\" prefix (which base
    /// folder BSShaderManager::GetTexture resolves against is not documented in
    /// CommonLib) and logs which one loaded. False when neither did: the recolour
    /// layers (projectile, explosion, hand art) stay off; the burn shader layer does
    /// not need it (the EFSH names its own palette).
    [[nodiscard]] bool LoadPalette();

    [[nodiscard]] bool PaletteReady() noexcept;

    /// The loaded texture (null before LoadPalette succeeded)
    [[nodiscard]] RE::NiSourceTexture* GetPalette() noexcept;

    /// The path that loaded, for BSEffectShaderMaterial::greyscaleTexturePath
    [[nodiscard]] const char* GetPalettePath() noexcept;

    /// A material's greyscale texture path names our palette: compared case-blind with both slash
    /// kinds alike and any leading "data" or "textures" folder ignored (BSFixedString's operator== is
    /// case-sensitive, while the game's string pool may hand back the path in another case)
    [[nodiscard]] bool IsPalettePath(const RE::BSFixedString& a_path);

    /// v2i: the additive palette shaders' palette (GradBlackFlameAdd.dds; the builder puts it on copies whose shape blends
    /// additively). Its colours are whatever DDS sits at that path. Loaded and logged by LoadPalette; the runtime recolour
    /// never uses it. Null when it did not load.
    [[nodiscard]] RE::NiSourceTexture* GetAddPalette() noexcept;

    /// The path names the additive palette (the same comparison as IsPalettePath, against its fixed path)
    [[nodiscard]] bool IsAddPalettePath(const RE::BSFixedString& a_path);

    /// Any palette of ours: either fixed one, or one the builder derived from a source palette (any greyscale texture under
    /// textures\heartofmagic\blackflame\, the same comparison)
    [[nodiscard]] bool IsAnyPalettePath(const RE::BSFixedString& a_path);
}
