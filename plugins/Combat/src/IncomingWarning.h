#pragma once

// =============================================================================
// IncomingWarning - spots spell missiles about to hit the player
// =============================================================================
// Hooks MissileProjectile::UpdateImpl (vtable slot 0xAB, CommonLib
// P/Projectile.h:211). After the game's own update, an enemy spell missile
// whose path passes within warnRadius of the player's chest and reaches it
// within warnLeadSeconds is reported to CounterPrompt (the SkyPrompt prompt)
// and kept as the nearest threat. Straight-line estimate:
// distance along the velocity / speed (spell missiles have no gravity).
// =============================================================================

namespace SLCombat
{
    class IncomingWarning
    {
    public:
        static void Install();

        /// Seconds until the nearest reported threat hits, aged since the report;
        /// -1 when there is none (no report in the last frames).
        static float NearestThreatSeconds();

    private:
        static void OnMissileUpdate(RE::Projectile* a_this, float a_delta);

        static inline REL::Relocation<decltype(OnMissileUpdate)> s_original;
    };
}
