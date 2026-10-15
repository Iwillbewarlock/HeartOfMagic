#pragma once

#include "Common.h"

// =============================================================================
// CONFIG - SKSE/Plugins/HeartOfMagic_BlackFlame.json
// =============================================================================
// Read once at plugin load, before any hook is installed or event sink
// registered; read-only afterwards, so every thread may read it.

namespace BlackFlame
{
    enum class BurnMode
    {
        kReplace,  // finish the engine's hit shader, then apply ours
        kOverlay   // leave the engine's hit shader, apply ours on top
    };

    struct Config
    {
        bool burnShader = true;   // layer 1: the burn shader on targets
        BurnMode burnMode = BurnMode::kReplace;
        bool projectile = true;   // layer 2: the projectile's 3D
        bool explosion = true;    // layer 3: the explosion's 3D
        bool handArt = true;      // layer 4: the casting art in the player's hands
        bool light = true;        // projectile, explosion and casting lights
        bool forcePalette = true; // add the grey-to-palette colour flag to effect shapes without it
        bool particleColors = false;  // also desaturate BSPSysSimpleColorModifier colours (unverified layout, Recolor.cpp)
        float tintScale = 0.6f;   // baseColor RGB = luminance * tintScale
        RE::NiColor lightColor{ 0.25f, 0.22f, 0.28f };
    };

    inline constexpr const char* kConfigPath = "Data/SKSE/Plugins/HeartOfMagic_BlackFlame.json";

    /// Reads kConfigPath; a missing file, bad JSON or a bad value keeps the default
    /// for what was wrong and logs it.
    void LoadConfig();

    [[nodiscard]] const Config& GetConfig() noexcept;

    [[nodiscard]] const char* BurnModeName(BurnMode a_mode) noexcept;
}
