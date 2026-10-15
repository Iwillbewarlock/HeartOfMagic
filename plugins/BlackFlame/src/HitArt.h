#pragma once

#include "Common.h"

// =============================================================================
// LAYER 1 - HIT ART of the player's fire effects (config hitArt)
// =============================================================================
// GetHitEffectArt is asked before the ModelReferenceEffect exists, so the hook only
// remembers the controller (its address, compared and never dereferenced later)
// and the art. Every ticker tick looks for the live ModelReferenceEffect whose
// controller and art match and recolours its 3D (artObject3D) once it is set; an
// art still without 3D after kMaxTicks ticks is dropped (logged).

namespace BlackFlame
{
    /// Any thread (the GetHitEffectArt hook): remember this controller's art for the next ticks
    void RememberHitArt(const RE::ActiveEffectReferenceEffectController* a_controller, RE::BGSArtObject* a_art);

    /// Ticker task (GameThread.h on what that guarantees): recolours the remembered arts whose 3D exists now
    void TickHitArt();

    /// Any thread: arts are waiting (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPendingHitArt() noexcept;

    /// Message handler, at kPreLoadGame / kNewGame: forgets every remembered art
    void ResetHitArt();
}
