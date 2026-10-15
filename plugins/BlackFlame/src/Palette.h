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
}
