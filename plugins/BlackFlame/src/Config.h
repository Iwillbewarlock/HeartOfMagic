#pragma once

#include "Common.h"

// =============================================================================
// CONFIG - SKSE/Plugins/HeartOfMagic_BlackFlame.json
// =============================================================================
// Read once at plugin load, before any hook is installed or event sink
// registered; read-only afterwards, so every thread may read it.

namespace BlackFlame
{
    struct Config
    {
        bool burnShader = true;   // layer 1: the hit shader of the player's fire effects swapped for ours
        bool hitArt = true;       // layer 1: the hit art of the player's fire effects recoloured
        bool projectile = true;   // layer 2: the projectile's 3D
        bool explosion = true;    // layer 3: the explosion's 3D
        bool hazard = true;       // layer 3: the player's fire hazards (ground fire): non-shared geometry and light
        bool handArt = true;      // layer 4: the casting art in the player's hands
        bool light = true;        // projectile, explosion and casting lights
        bool forcePalette = true; // add the grey-to-palette colour flag to effect shapes without it
        bool selfTest = false;    // run the scripted self-test once after a save is loaded (SelfTest.h)
        bool particleColors = false;  // also desaturate BSPSysSimpleColorModifier colours (unverified layout, Recolor.cpp)
        float tintScale = 0.6f;   // baseColor RGB = luminance * tintScale
        RE::NiColor lightColor{ 0.25f, 0.22f, 0.28f };
    };

    inline constexpr const char* kConfigPath = "Data/SKSE/Plugins/HeartOfMagic_BlackFlame.json";

    /// Reads kConfigPath; a missing file, bad JSON or a bad value keeps the default
    /// for what was wrong and logs it.
    void LoadConfig();

    [[nodiscard]] const Config& GetConfig() noexcept;
}
