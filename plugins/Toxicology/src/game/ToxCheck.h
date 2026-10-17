#pragma once

// =============================================================================
// Toxicology - debug check of the Magicka count
// =============================================================================
// Spec 5.6: every kCheckWindowSeconds of poison casting the log gets
// "magicka counted X (left a, right b, no hand c) / player magicka drop Y". With
// regeneration and potions out of the way the two should agree within 5%. The
// first 3 windows after each load or new game go to the log at info, the rest at
// debug (the log runs at info).
//
// Threads: game main thread only (ToxMagicka's tasks, ResetMagicka).
// =============================================================================

#include <cstddef>

namespace ToxGame
{
    inline constexpr double kCheckWindowSeconds = 5.0;
    // A pause longer than this between counted amounts starts a new window (longer than a slow
    // fire-and-forget charge); ToxMagicka also uses it for its once-per-stretch release log
    inline constexpr double kCheckGapSeconds = 4.0;

    // Hand index of a counted amount: 0 left, 1 right (MagicSystem::CastingSource), kCheckNoHand when
    // the cost was not taken from a hand caster
    inline constexpr std::size_t kCheckNoHand = 2;

    // A counted amount from a real cast. The first amount of a window was charged before the window
    // reads the player's Magicka, so it opens the window and is left out of both sides.
    void NoteCountedForCheck(float a_amount, std::size_t a_hand);

    // Closes the window without logging and counts info windows from 0 again (co-save Revert, new game)
    void ResetCheck();
}
