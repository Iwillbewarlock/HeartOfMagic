#pragma once

#include <algorithm>

// =============================================================================
// PROGRESSION MATH - the XP cap, study and mastery arithmetic
// =============================================================================
//
// The numbers ProgressionManager and the ISL study callback work out, kept apart
// from the game types so tools/progression-check.cpp runs the same code against
// made-up cases. XP values are absolute XP; progress is a fraction of the
// required XP (0..1). Every function is O(1).
namespace ProgressionMath
{
    // Study XP (ISL) never takes a spell to 1.0 on its own: ISL has not taught
    // the spell yet, and mastery at 1.0 would mark it learned without it. The
    // last step comes from a cast once ISL has taught it (ProgressionManager::AddXP).
    inline constexpr float kNoGrantMaxProgress = 0.99f;

    // What is left under a cap: never negative
    [[nodiscard]] constexpr float CapRemaining(float capXP, float trackedXP) noexcept
    {
        return (std::max)(0.0f, capXP - trackedXP);
    }

    // An amount cut to what is left under the cap (0 for a non-positive amount)
    [[nodiscard]] constexpr float GrantWithinCap(float amount, float capXP, float trackedXP) noexcept
    {
        if (!(amount > 0.0f)) return 0.0f;  // NaN too
        return (std::min)(amount, CapRemaining(capXP, trackedXP));
    }

    // The amount to ask for so that, once the multiplier is applied, the grant
    // stays inside `remainingXP`. A source that applies its own cap before
    // AddSourcedXP multiplies (passive learning) uses it. 0 when the multiplier is
    // not positive (the grant would be 0 anyway).
    [[nodiscard]] constexpr float PreMultiplierGrant(float baseXP, float remainingXP, float multiplier) noexcept
    {
        if (!(baseXP > 0.0f) || !(remainingXP > 0.0f) || !(multiplier > 0.0f)) return 0.0f;
        return (std::min)(baseXP, remainingXP / multiplier);
    }

    // SetSpellXP: the progress an XP value sets, clamped to 0..1 of the required
    // XP. 0 when the required XP is not set (never marks a spell mastered), and
    // for NaN from the API or Papyrus (std::clamp lets NaN through, and a NaN
    // progress would stay in the co-save for good).
    [[nodiscard]] constexpr float ProgressForXP(float xp, float requiredXP) noexcept
    {
        if (!(xp > 0.0f) || !(requiredXP > 0.0f)) return 0.0f;
        return std::clamp(xp / requiredXP, 0.0f, 1.0f);
    }

    // ISL study: one session's XP, cut so that all study XP of a spell stays
    // within its share of the required XP (the early unlock threshold). Swapping
    // books restarts ISL's own count, not this one.
    [[nodiscard]] constexpr float StudyGrant(float sessionXP, float requiredXP, float thresholdFraction,
                                             float studiedXP) noexcept
    {
        return GrantWithinCap(sessionXP, requiredXP * (std::max)(0.0f, thresholdFraction), studiedXP);
    }

    // AddXPNoGrant: the new progress, stopped at kNoGrantMaxProgress and never
    // lowered (a save already stuck at 1.0 keeps it, and the next cast masters it).
    [[nodiscard]] constexpr float NoGrantProgress(float oldProgress, float amount, float requiredXP) noexcept
    {
        if (!(requiredXP > 0.0f)) return 0.0f;
        const float added = (oldProgress * requiredXP + (std::max)(0.0f, amount)) / requiredXP;
        return (std::max)(oldProgress, (std::min)(added, kNoGrantMaxProgress));
    }

    // AddXP with early learning on: mastery fires when progress crosses 1.0 now,
    // or when it was already there, not unlocked (a study overflow saved by an
    // older version left it at 1.0 unlearned), and the spell is early learned AND
    // the player has it (weakened). A spell held
    // for ISL that ISL has not taught yet waits: mastery does not add the spell. A
    // spell set to 100% by cheat mode and never granted stays "ready" for Unlock.
    [[nodiscard]] constexpr bool ShouldMaster(float oldProgress, float newProgress, bool unlocked,
                                              bool earlyLearnedAndKnown) noexcept
    {
        // Crossing 1.0 masters even an unlocked spell (a cheat-lowered one climbing
        // back), as before: its target is cleared and the panel told
        return newProgress >= 1.0f && (oldProgress < 1.0f || (!unlocked && earlyLearnedAndKnown));
    }
}
