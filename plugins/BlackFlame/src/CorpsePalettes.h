#pragma once

#include "Common.h"

// =============================================================================
// CORPSE PALETTES (design v2, 2026-10-20) - the grey palettes for a marked corpse's ShaderReferenceEffect
// =============================================================================
// The builder writes a grey copy of every effect shader palette (TESEffectShader particle palette NAM9 and membrane
// palette NAM8) that passes its fire test; the copies are loose DDS files, no plugin record, so there is no slot limit.
// LoadAtGate takes them into the texture manager on the main thread at the gate (W1 kDataLoaded, W2 main menu) and
// keeps a strong reference for the session; Find hands out the grey texture for a source palette path afterwards.
// CorpseGrey (hooks A and B) is the only reader.

namespace BlackFlame::CorpsePalettes
{
    /// Main thread at the gate: loads every published grey palette (a fallback texture from the manager is refused).
    /// Returns how many are ready; logs one summary line
    std::size_t LoadAtGate();

    /// Any thread after LoadAtGate: the grey texture for a source palette path as TESTexture holds it (any case, with or
    /// without the "textures\" prefix), or nullptr (none built, not loaded, or not fire). The registry keeps it alive
    [[nodiscard]] RE::NiSourceTexture* Find(std::string_view a_sourcePath) noexcept;

    /// "N grey palettes ready of M built" for the stats line
    [[nodiscard]] std::string Stats();
}
