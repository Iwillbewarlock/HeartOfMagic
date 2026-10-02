#pragma once

// =============================================================================
// WardSupport - the ward side of the counter
// =============================================================================
//   - the counter needs the player to know at least one ward spell;
//   - after a counter the ward stays up wardLingerSeconds even if the key is let
//     go (free), for safety;
//   - holding the counter key after a counter keeps a real ward up: the
//     player's strongest known ward is cast on them every refresh while the
//     key stays down, and its magicka cost per second is charged;
//   - with spells in both hands and the Restoration dual-casting perk, the
//     counter's ward and the held ward are dual-cast (stronger, costlier);
//   - "360 Ward.esp" (360 Ward by Averoze) makes wards a full sphere: with it
//     loaded, counters work from every direction; without it, within frontAngle.
//   Blocking streams and breath shouts with a ward is left to ward mods
//   (Perfectly Valid Wards and the like).
// =============================================================================

#include "CounterInput.h"

namespace SLCombat
{
    class WardSupport
    {
    public:
        /// Called at kDataLoaded: looks for 360 Ward and the dual-cast perk.
        static void Detect();

        /// Spells in both hands and the Restoration dual-cast perk: the ward is dual-cast.
        static bool CanDualCast(RE::PlayerCharacter* a_player);

        /// The strongest ward spell the player knows (by Ward Power magnitude), or null.
        static RE::SpellItem* BestKnownWard(RE::PlayerCharacter* a_player);

        /// Cast the strongest known ward on the player now (the counter's ward).
        /// Returns Ward Power right after, for the log.
        static float RaiseWard(RE::PlayerCharacter* a_player);

        /// True with 360 Ward loaded (and sphereWardAllAngles on): no facing limit.
        static bool AllAngles();

        /// The dual-cast perk's name, or why there is none (the settings page).
        static std::string DualCastPerkName();

        // --- held ward (game thread): refreshed on a timer while the key is held
        static void StartHold();
        static void ReleaseHold();

    private:
        static void CastWard(RE::PlayerCharacter* a_player, RE::SpellItem* a_ward, bool a_dual);
        static void Refresh(RE::PlayerCharacter* a_player);
        static void EnsureLoop();
        static void ScheduleTick();
        static void Tick();
        static void StopHold(const char* a_reason);
    };
}
