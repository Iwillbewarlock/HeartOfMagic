#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - HIT ART of the player's fire effects (config hitArt)
// =============================================================================
// GetHitEffectArt is asked before the ModelReferenceEffect exists, so the hook only
// remembers the controller (its address, to find the effect it made) and the art
// (not the target: the first call comes before the engine sets it). Every ticker
// tick looks for the live ModelReferenceEffect whose controller and art match,
// reading the target from that live effect (only comparisons and NiPointer copies inside
// the engine's ProcessLists lock), checks after the walk that its live controller
// still names a fire effect the player cast, and recolours its 3D (artObject3D)
// once it is set; an art still without 3D after kMaxTicks ticks is dropped (logged).

namespace BlackFlame
{
    /// Any thread (the GetHitEffectArt hook): remember this controller's art for the next ticks (the
    /// controller is read again only through the live effect it made, after the ProcessLists walk;
    /// a_controller's target is not read here)
    void RememberHitArt(const RE::ActiveEffectReferenceEffectController* a_controller, RE::BGSArtObject* a_art);

    /// Ticker task (GameThread.h on what that guarantees): recolours the remembered arts whose 3D exists now
    void TickHitArt();

    /// Any thread: arts are waiting (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPendingHitArt() noexcept;

    /// Message handler, at kPreLoadGame / kNewGame: forgets every remembered art
    void ResetHitArt();
}
