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

    // A cap in percent of the required XP that cuts nothing (self casts, internal sources)
    inline constexpr float kNoCapPercent = 100.0f;

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

    // Passive learning: game hours (Calendar::GetHoursPassed - GetCurrentGameTime
    // is days) between ticks below this give nothing yet and keep the last time,
    // so short ticks add up instead of being lost
    inline constexpr float kPassiveMinHours = 0.1f;

    // One passive tick from the last time granted to now, both in game hours: the
    // hours to grant (0 for none) and the time to keep as the last one. Time going
    // back (an earlier save, a time mod) grants nothing and starts again from now.
    struct PassiveTick
    {
        float grantHours = 0.0f;
        float lastHours = 0.0f;
    };
    [[nodiscard]] constexpr PassiveTick PassiveTickFor(float lastHours, float nowHours) noexcept
    {
        const float elapsed = nowHours - lastHours;
        if (elapsed > kPassiveMinHours) return { elapsed, nowHours };
        if (elapsed < 0.0f || !(elapsed == elapsed)) return { 0.0f, nowHours };  // back in time, or NaN
        return { 0.0f, lastHours };
    }

    // Passive learning's XP for `hours` game hours at `xpPerDay` per game day (the
    // setting is per day: 5 a day was the feel players knew, and the slider's 1 step
    // cannot go below an hour's share of that)
    inline constexpr float kGameHoursPerDay = 24.0f;
    [[nodiscard]] constexpr float PassiveXPFor(float xpPerDay, float hours) noexcept
    {
        if (!(xpPerDay > 0.0f) || !(hours > 0.0f)) return 0.0f;
        return xpPerDay * hours / kGameHoursPerDay;
    }

    // AddXP: the progress after `amount` XP, at most 1. A required XP that is not
    // set (0 or less) leaves the progress as it is: no XP is counted, and the saved
    // progress is not wiped either.
    [[nodiscard]] constexpr float ProgressAfterXP(float oldProgress, float amount, float requiredXP) noexcept
    {
        if (!(requiredXP > 0.0f)) return oldProgress;
        const float newXP = (std::min)(oldProgress * requiredXP + amount, requiredXP);
        return (std::min)(newXP / requiredXP, 1.0f);
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

    // AddXP with early learning on: mastery fires when progress crosses 1.0 now
    // (even for an unlocked spell lowered by cheat mode, as before), or when it was
    // already there, not unlocked (a study overflow saved by an older version left
    // it at 1.0 unlearned), and the spell is early learned and the player has it.
    // Never for a spell held for ISL that ISL has not taught yet (early learned,
    // not owned): mastery adds no spell, and ISL teaching it later would put a
    // mastered spell back in the early set - it stays "ready" until taught, and the
    // next XP after that masters it. A spell set to 100% by cheat mode and never
    // granted stays "ready" for Unlock. Crossing for a spell that is not early
    // learned (a copy known) masters as before, owned or not.
    [[nodiscard]] constexpr bool ShouldMaster(float oldProgress, float newProgress, bool unlocked,
                                              bool earlyLearned, bool playerHasSpell) noexcept
    {
        if (newProgress < 1.0f || (earlyLearned && !playerHasSpell)) return false;
        return oldProgress < 1.0f || (!unlocked && earlyLearned);
    }
}
