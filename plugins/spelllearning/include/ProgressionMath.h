#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

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
    // While passive learning is off (`enabled` false) the last time follows the clock and
    // nothing is granted: switching it on pays only the hours from then, not the ones off.
    [[nodiscard]] constexpr PassiveTick PassiveTickFor(float lastHours, float nowHours, bool enabled = true) noexcept
    {
        if (!enabled) return { 0.0f, nowHours };
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

    // The XP a grant really added: the progress it moved times the required XP. A grant asked
    // at 99% that stops at 100% added 1% of the required XP; one on a spell already at 100%
    // (ready, not unlocked) added nothing. Never negative; 0 for NaN or no required XP. What
    // AddSourcedXP returns and counts, and what a cast counts, so BookXP keeps a book that
    // added nothing for later.
    [[nodiscard]] constexpr float ActualRise(float oldProgress, float newProgress, float requiredXP) noexcept
    {
        if (!(requiredXP > 0.0f)) return 0.0f;
        const float moved = newProgress - oldProgress;
        return moved > 0.0f ? moved * requiredXP : 0.0f;
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

    // AddXP with early learning on, and the settings re-check: whether to try the early
    // (weakened) grant. Not when the spell is granted already (early learned, held for
    // ISL among them) or below the threshold. Any spell crossing the threshold now, as
    // before; a learning target also when it was above it already - a threshold lowered
    // or early learning switched on - unless it was at 1.0 before (set there by cheat
    // mode, never granted: it stays "ready" for Unlock). A spell that is not a target
    // stays out once above: a target change took its weakened copy away on purpose
    // (RemoveEarlySpellFromPlayer). The grant rule (ShouldGrantEarly) still decides.
    [[nodiscard]] constexpr bool ShouldTryEarlyGrant(float oldProgress, float newProgress, float thresholdFraction,
                                                     bool isTarget, bool alreadyGranted) noexcept
    {
        if (alreadyGranted || !(newProgress >= thresholdFraction)) return false;  // NaN too
        return oldProgress < thresholdFraction || (isTarget && oldProgress < 1.0f);
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

    // A cast of another plugin's copy of a tree spell (SpellScanner::DuplicateGroupOf) is that spell
    // to the tree: true when `matches` holds for the cast spell or any of its copies. Used for the
    // direct link of a cast (IsDirectPrerequisite, IsDirectChild). O(copies).
    template <class Ids, class Pred>
    [[nodiscard]] bool SpellOrCopyMatches(std::uint32_t spellId, const Ids& copies, Pred&& matches)
    {
        if (matches(spellId)) return true;
        for (const auto copyId : copies) {
            if (matches(copyId)) return true;
        }
        return false;
    }

    // What a cast is to a learning target, in single mode's order of preference: the target
    // itself, a direct link (either way), the same school, any other spell
    enum class CastSource : std::uint8_t
    {
        kSelf,
        kDirect,
        kSchool,
        kAny,
    };

    [[nodiscard]] constexpr const char* CastSourceName(CastSource source) noexcept
    {
        switch (source) {
            case CastSource::kSelf: return "self";
            case CastSource::kDirect: return "direct";
            case CastSource::kSchool: return "school";
            case CastSource::kAny: return "any";
        }
        return "any";
    }

    // Single learning mode credits one learning target per cast: `items` reordered so the closest
    // link comes first (CastSource order), items of one source keeping their order. `sources[i]`
    // is items[i]'s source; a length mismatch leaves `items` as it is. O(n log n).
    template <class T>
    void OrderBySource(std::vector<T>& items, const std::vector<CastSource>& sources)
    {
        if (items.size() != sources.size()) return;
        std::vector<std::size_t> order(items.size());
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sources[a] < sources[b]; });
        std::vector<T> sorted;
        sorted.reserve(items.size());
        for (const auto i : order) sorted.push_back(std::move(items[i]));
        items = std::move(sorted);
    }

    // =========================================================================
    // Per-source XP counts (the caps) against the XP the spell holds
    // =========================================================================

    // The counts may be a hair over the XP from float sums; only more than this share is cut
    inline constexpr float kTrackerSlack = 1e-4f;

    // What every per-source count is multiplied by so that their sum is at most the XP the
    // spell holds now: 1 when it already is, 0 when the spell holds nothing (or NaN). Without it
    // a progress lowered by cheat mode, SetSpellXP or a smaller required XP kept the counts where
    // they were, so a source at its cap stayed there and gave nothing again.
    [[nodiscard]] constexpr float TrackerScale(float currentXP, float trackedTotal) noexcept
    {
        if (!(currentXP > 0.0f)) return 0.0f;
        if (!(trackedTotal > currentXP * (1.0f + kTrackerSlack))) return 1.0f;
        return currentXP / trackedTotal;
    }

    // The per-source counts are shares of the required XP, as the progress is: when a spell's required
    // XP changes from `oldRequired` to `newRequired` (an override set or removed, the reverse unlock share,
    // the tree's number arriving after a load), every count is scaled by new / old - both ways, so a
    // required XP lowered then raised gives the counts back, and a cap closed at 15% stays closed at 15%.
    // Nothing when either is not positive or they are equal. True when it changed. Shrinking the counts
    // to the XP held (FitTrackersToXP) is only for a lowered progress (SetSpellXP): a required XP change
    // moves the progress's XP by the same factor, so the counts' sum stays within it.
    template <class Progress>
    bool RescaleTrackers(Progress& progress, float oldRequired, float newRequired)
    {
        if (!(oldRequired > 0.0f) || !(newRequired > 0.0f) || oldRequired == newRequired) return false;
        const float scale = newRequired / oldRequired;
        progress.xpFromAny *= scale;
        progress.xpFromSchool *= scale;
        progress.xpFromDirect *= scale;
        progress.xpFromSelf *= scale;
        for (auto& source : progress.xpFromModded) source.second *= scale;
        return true;
    }

    // TrackerScale applied to a progress entry (any struct with xpFromAny, xpFromSchool,
    // xpFromDirect, xpFromSelf and a name -> XP map xpFromModded). True when it changed.
    template <class Progress>
    bool FitTrackersToXP(Progress& progress, float currentXP)
    {
        float total = progress.xpFromAny + progress.xpFromSchool + progress.xpFromDirect + progress.xpFromSelf;
        for (const auto& source : progress.xpFromModded) total += source.second;
        if (total == 0.0f) return false;
        const float scale = TrackerScale(currentXP, total);
        if (scale >= 1.0f) return false;
        progress.xpFromAny *= scale;
        progress.xpFromSchool *= scale;
        progress.xpFromDirect *= scale;
        progress.xpFromSelf *= scale;
        for (auto& source : progress.xpFromModded) source.second *= scale;
        return true;
    }

    // =========================================================================
    // Counts from a save that does not say what required XP they were made against (SLPX v1, a
    // 1.7 save): absolute XP, maybe still stuck above the XP held by an old cheat-mode lowering (fix A
    // came after). They are fitted to the XP held once, when the spell's required XP is no longer the
    // load's passing number: at load end when the tree's numbers were already applied this session (a
    // tree read at the main menu), else after the tree's numbers are in (SyncRequiredXPFromTree), at the
    // first required XP change (fitted at the number before it, then scaled - never left unscaled), or
    // at the spell's first XP. Until then a save writes no basis for them (the next load marks them
    // again). Any struct with the counts, progressPercent, requiredXP and a bool fitPending.
    // =========================================================================

    // At load end, for an entry with no saved basis: pending when it has any count. True when marked.
    template <class Progress>
    bool MarkFitPending(Progress& progress)
    {
        bool any = progress.xpFromAny != 0.0f || progress.xpFromSchool != 0.0f || progress.xpFromDirect != 0.0f ||
                   progress.xpFromSelf != 0.0f;
        for (const auto& source : progress.xpFromModded) any = any || source.second != 0.0f;
        progress.fitPending = any;
        return any;
    }

    // The one-time fit: FitTrackersToXP against the XP held now, the mark cleared (the counts are then
    // shares of the current required XP, as every other entry's). True when it was pending.
    template <class Progress>
    bool SettlePendingFit(Progress& progress)
    {
        if (!progress.fitPending) return false;
        progress.fitPending = false;
        FitTrackersToXP(progress, progress.progressPercent * progress.requiredXP);
        return true;
    }

    // At load end, for an entry with no saved basis: marked (MarkFitPending), and settled at once when the
    // tree's required XP numbers were already applied this session - the number now is final, and no tree
    // read follows to settle it (review 2026-11-04: a tree read at the main menu, then a v1 save loaded).
    // True when it has counts to fit.
    template <class Progress>
    bool LoadEndFit(Progress& progress, bool treeRequiredApplied)
    {
        if (!MarkFitPending(progress)) return false;
        if (treeRequiredApplied) SettlePendingFit(progress);
        return true;
    }

    // A required XP change from `oldRequired`: a pending fit is settled first, at `oldRequired` (the number in
    // force until now), then every count is scaled as RescaleTrackers says. Never a change left unscaled: that
    // reopened a cap (100 -> 200 kept 5/15 where 10/30 is the share). True when anything changed.
    template <class Progress>
    bool RescaleSettlingFirst(Progress& progress, float oldRequired, float newRequired)
    {
        bool changed = false;
        if (progress.fitPending) {
            progress.fitPending = false;
            changed = FitTrackersToXP(progress, progress.progressPercent * oldRequired);
        }
        return RescaleTrackers(progress, oldRequired, newRequired) || changed;
    }

    // The basis SLPX v2 saves: the required XP, or 0 (not known) while the fit is pending
    template <class Progress>
    [[nodiscard]] float BasisToSave(const Progress& progress)
    {
        return progress.fitPending ? 0.0f : progress.requiredXP;
    }
}
