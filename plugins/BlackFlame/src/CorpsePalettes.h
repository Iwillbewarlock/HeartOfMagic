#pragma once

#include "Common.h"

#include "CopyContractEfsh.h"

// =============================================================================
// CORPSE PALETTES (design v2, 2026-10-20; v2.1 base textures) - the grey textures for a marked corpse's ShaderReferenceEffect
// =============================================================================
// The builder writes a grey copy of every effect shader palette (TESEffectShader particle palette NAM9 and membrane
// palette NAM8) that passes its fire test, and (contract 8) of every base texture whose RGB a shader draws (membrane fill
// ICON, particle base ICO2: CopyContractEfshBase.h EfshBaseRoles) that passes its own; the copies are loose DDS files, no
// plugin record, so there is no slot limit. LoadAtGate takes both kinds into the texture manager on the main thread at
// the gate (W1 kDataLoaded, W2 main menu) and keeps a strong reference for the session; Find / FindBase hand out the grey
// texture for a source path afterwards. The two kinds are kept apart: one source path can be a palette for one shader and
// a base for another, and the two recipes differ (palette.dds linear-light luminance, copy.dds encoded-domain, all mips).
// CorpseGrey (hooks A and B) is the only reader.

namespace BlackFlame::CorpsePalettes
{
    /// Main thread at the gate: loads every published grey palette and base texture (a fallback texture from the manager
    /// is refused). Returns how many palettes are ready; logs one summary line per kind
    std::size_t LoadAtGate();

    /// Any thread after LoadAtGate: the grey texture for a source palette path as TESTexture holds it (any case, with or
    /// without the "textures\" prefix), or nullptr (none built, not loaded, or not fire). The registry keeps it alive
    [[nodiscard]] RE::NiSourceTexture* Find(std::string_view a_sourcePath) noexcept;

    /// The same for a source base texture path (contract 8: a membrane fill or particle base the builder greyed)
    [[nodiscard]] RE::NiSourceTexture* FindBase(std::string_view a_sourcePath) noexcept;

    /// A shader's flags as the contract's rules read them (EfshPaletteRoles, EfshBaseRoles)
    [[nodiscard]] CopyContract::EfshShaderFlags FlagsOf(const RE::EffectShaderData& a_data) noexcept;

    /// "N grey palettes ready of M built; N grey base textures ready of M built" for the stats line
    [[nodiscard]] std::string Stats();
}
