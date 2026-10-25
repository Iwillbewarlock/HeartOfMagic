#pragma once

#include "Common.h"

#include <string>

// =============================================================================
// CORPSE GREY (design v2 §3) - a marked corpse's ShaderReferenceEffect draws grey textures and instance colours
// =============================================================================
// The mark (CorpseMark.h: SRE flags bit 24) is set at construction; this file acts on it when the SRE attaches.
// Hook A, write_call at 34933 +0x12D (5D1B8D), 34934's only caller (rcx = the SRE). Before the original, for an SRE with
//   the bit while greyCorpseShaders is on: CorpsePalettes::Find for the paths its effectData (TESEffectShader) holds,
//   NAM9 particlePaletteTexture (loaded into SRE +0xD8 by Init 34912) and NAM8 membranePaletteTexture (+0xF0); each one
//   found replaces that NiPointer member (reset: the grey gains a reference, the source loses one; the dtor releases
//   it). v2.1 (contract 8): CorpsePalettes::FindBase the same way for the base textures this SRE's own shader draws as
//   colour (CopyContractEfshBase.h EfshBaseRoles on its flags, checked again here): ICON fillTexture (+0xE0) and ICO2
//   particleShaderTexture (+0xD0); a stage with greyscale to colour keeps its base (its green is the palette coordinate).
//   Pop 34925 refills the membrane data from the members, so the grey textures survive it. A thread_local "private
//   particles" flag is up for every marked SRE's original call (a scope guard), even without a texture copy: a mod can
//   patch its live EFSH colour keys. "corpse textures kept [n]" logs missing copies separately from instance colours. The line
//   "corpse attach [n]: ..." (48 a session) says what was swapped and what was kept.
// Hook B, write_call at 34934 +0x2EC (5D1F1C), 17325's only caller (rcx effectData, rdx SRE +0xD0, r8 SRE +0xD8 read just
//   before the call, r9b share): while the flag is up, the original runs with share false, so the particle property is
//   this SRE's own and the shader's shared cache (TESEffectShader +0x210 / +0x218) is neither read nor filled; r8 is
//   already the grey palette and rdx the grey base when hook A replaced them. The returned private property's three
//   colour keys become luminance RGB, alpha kept, including when share was already false. Otherwise forwarded unchanged.
//   "corpse particles private [n]" (20 a session) logs the cache before and after; "corpse particle colours" the live keys.
// CorpseColours.h: after attach and the chained Update/Pop vfuncs, grey the instance membrane's fill/rim RGB again.
//   Update interpolates fill colour every frame; Pop refills both colours. effectData, its keys and its identity are
//   never changed (other actors, saves, EffectShader.Stop and HasEffectShader still see the original EFSH).
// Both sites' bytes (the E8 and the argument loads before it) are checked first; AE 1.6.1170 and 1.7.104 only. Any
// mismatch installs neither (a grey +0xD8 with share true would be cached for every SRE of that shader), and the mark then has no effect.
// Threads: 34934 runs where SRE Update runs (H1: logged on the first call); the flag is thread_local, the counters
// atomic, CorpsePalettes::Find any thread.

namespace BlackFlame::HitPath::Detail
{
    /// HitPath::Install after its sites (1.6.1170 / 1.7.104, ids checked there, 17325 included): checks both sites, then
    /// hooks both or neither
    void InstallCorpseGrey();

    /// "attach 34934 N calls, ..." for the corpse mark's stats line
    [[nodiscard]] std::string CorpseGreyStats();
}
