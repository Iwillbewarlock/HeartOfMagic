#pragma once

#include "Common.h"

// =============================================================================
// DRAW-TIME GREY (dev spike, config devDrawTimeGrey, default off)
// =============================================================================
// Asks one question by data: can the player's fire turn grey without copy files, by binding our palette into the
// palette texture slot while the engine draws one of the PLAYER's effect geometries (Tier 0 of the draw-time design)?
//
// - BSEffectShader SetupGeometry / RestoreGeometry (BSShader.h vfuncs 06 / 07) are hooked on VTABLE_BSEffectShader[0].
//   After the original SetupGeometry, while stoked, a pass whose geometry is in the player set gets our palette's view
//   written into the renderer shadow state's PS texture slot that holds the shape's own palette (the slot is MEASURED:
//   the first index of PSTexture[] that equals the material's greyscale texture view). RestoreGeometry puts the saved
//   view back first, whatever the buff is by then, then calls the original.
// - The player set (DrawTimeGreySet.cpp): projectiles and explosions the existing hooks attribute to the player
//   (NotePlayerRef) and the casting art in the player's hands, walked on the main thread; their effect geometries are
//   held by NiPointer on the main thread only, and the render side reads an immutable snapshot of raw pointers.
// - Every 5 s while stoked one summary line (DrawTimeGreyHook.cpp, Summary()).
// Nothing is installed or started when the key is off.

namespace BlackFlame
{
    /// kDataLoaded, after the palettes are loaded: hooks the two vfuncs and starts the 100 ms rewalk ticker (key off:
    /// nothing)
    void InstallDrawTimeGrey();

    /// Any thread (Projectile Handle3DLoaded, Explosion Set3D): a reference the attribution gave to the player; its
    /// 3D is walked on the main thread at the next drain. a_kind: a string literal for the logs. Nothing while the key
    /// is off or the buff is not on.
    void NotePlayerRef(RE::TESObjectREFR* a_ref, const char* a_kind);

    /// Message handler, at kPreLoadGame / kNewGame: the set and its roots dropped (an empty snapshot published first)
    void ResetDrawTimeGrey();
}
