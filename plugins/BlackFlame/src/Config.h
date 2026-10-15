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
        bool hitArt = false;      // layer 1: runtime recolour of the hit art (v2: off by default, retired with the copies)
        bool projectile = false;  // layer 2: runtime recolour of the projectile's 3D (v2: off by default)
        bool explosion = false;   // layer 3: runtime recolour of the explosion's 3D (v2: off by default)
        bool hazard = false;      // layer 3: runtime recolour of the player's fire hazards' non-shared geometry (v2: off)
        bool handArt = false;     // layer 4: runtime recolour of the casting art in the player's hands (v2: off)
        bool boundWeapon = false; // layer 4: runtime recolour of the player's bound fire weapons' 3D (v2: off)
        bool scriptPlaced = true; // what scripts of the player's fire effects place or cast from (PapyrusWrap.h)
        bool impactSwap = true;   // the player's fire impacts load the grey copies of the grey map (ImpactSwap.h)
        bool hazardSwap = true;   // the player's fire hazards load the grey copies of the grey map (HazardSwap.h)
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
