#pragma once

#include "Common.h"

#include <string>

// =============================================================================
// CORPSE GREY (design v2 §3, 2026-10-20) - a marked corpse's ShaderReferenceEffect draws grey palettes
// =============================================================================
// The mark (CorpseMark.h: SRE flags bit 24) is set at construction; this file acts on it when the SRE attaches.
// Hook A, write_call at 34933 +0x12D (5D1B8D), 34934's only caller (rcx = the SRE). Before the original, for an SRE with
//   the bit while greyCorpseShaders is on: CorpsePalettes::Find for the paths its effectData (TESEffectShader) holds,
//   NAM9 particlePaletteTexture (loaded into SRE +0xD8 by Init 34912) and NAM8 membranePaletteTexture (+0xF0); each one
//   found replaces that NiPointer member (reset: the grey gains a reference, the source loses one; the dtor releases
//   it). Pop 34925 refills the membrane data from the members, so the grey survives it. When +0xD8 was replaced a
//   thread_local "private particles" flag is up for the original call only (a scope guard). No grey for either: the
//   original runs unchanged and "corpse kept [n]: no grey palette for '<path>'" is logged (48 a session). The line
//   "corpse attach [n]: ..." (48 a session) says what was swapped and what was kept.
// Hook B, write_call at 34934 +0x2EC (5D1F1C), 17325's only caller (rcx effectData, rdx SRE +0xD0, r8 SRE +0xD8 read just
//   before the call, r9b share): while the flag is up, the original runs with share false, so the particle property is
//   this SRE's own and the shader's shared cache (TESEffectShader +0x210 / +0x218) is neither read nor filled; r8 is
//   already the grey palette (hook A replaced +0xD8). Otherwise forwarded unchanged. "corpse particles private [n]"
//   (20 a session) logs the cache before and after.
// Both sites' bytes (the E8 and the argument loads before it) are checked first; AE 1.6.1170 only. Any mismatch installs
// neither (a grey +0xD8 with share true would be cached for every SRE of that shader), and the mark then has no effect.
// Threads: 34934 runs where SRE Update runs (H1: logged on the first call); the flag is thread_local, the counters
// atomic, CorpsePalettes::Find any thread.

namespace BlackFlame::HitPath::Detail
{
    /// HitPath::Install after its sites (1.6.1170, ids checked there, 17325 included): checks both sites, then hooks both or
    /// neither
    void InstallCorpseGrey();

    /// "attach 34934 N calls, ..." for the corpse mark's stats line
    [[nodiscard]] std::string CorpseGreyStats();
}
