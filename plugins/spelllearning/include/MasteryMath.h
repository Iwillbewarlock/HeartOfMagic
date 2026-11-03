#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

// =============================================================================
// MasteryMath - the ranks past mastery, the numbers
//
// A mastered spell keeps earning XP when the player casts it, toward `ranks` mastery
// ranks (3 by default). The XP a rank costs follows the spell's tier (Novice ...
// Master, mastery.tierXP) times that rank's multiplier (mastery.rankMultipliers);
// the school's skill level raises the XP a cast gives, not what a rank costs, so a
// lower skill later (Legendary) never takes a rank away. The saved number is the raw
// XP: the rank is worked out from it and the settings each time (nothing to go
// stale when a setting changes), and a setting that moves the bar never cuts it.
// Design: docs/ARCHITECTURE.md, "Mastery ranks".
//
// Pure (no game types): ProgressionManagerMastery.cpp and tools/progression-check
// --self-test run the same code. Every function is O(ranks) at most. The config.json reader is
// MasteryConfig.h (kept apart: this header is in ProgressionManager.h, nlohmann/json is not).
// =============================================================================

namespace MasteryMath
{
    // The scan's tiers (SpellScanner::DetermineSpellTier), index 0 Novice ... 4 Master
    inline constexpr std::size_t kTierCount = 5;
    inline constexpr std::array<std::string_view, kTierCount> kTierKeys{ "novice", "apprentice", "adept", "expert",
                                                                          "master" };
    // The skill a tier's spells start at (the vanilla perk levels): a modded Master spell often
    // has minimumSkill 0 on its effect, so the tier, not the effect, says where the skill factor starts
    inline constexpr std::array<float, kTierCount> kTierMinSkill{ 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };
    inline constexpr float kFullSkill = 100.0f;

    // Settings (config.json "mastery": {...}); the defaults are starting values, not measured
    inline constexpr bool kDefaultEnabled = true;
    inline constexpr int kDefaultRanks = 3;
    inline constexpr int kMinRanks = 1;
    inline constexpr int kMaxRanks = 10;
    inline constexpr std::array<float, kTierCount> kDefaultTierXP{ 100.0f, 200.0f, 400.0f, 800.0f, 1500.0f };
    inline constexpr float kMaxTierXP = 1'000'000.0f;
    inline constexpr float kDefaultRankMultiplier = 1.0f;
    inline constexpr float kMaxRankMultiplier = 100.0f;
    inline constexpr float kDefaultSkillAtMin = 1.0f;
    inline constexpr float kDefaultSkillAt100 = 2.0f;
    inline constexpr float kMaxSkillFactor = 10.0f;
    inline constexpr float kDefaultXPRate = 1.0f;
    inline constexpr float kMaxXPRate = 100.0f;

    // A saved value past this is not one the settings can reach (kMaxTierXP x kMaxRankMultiplier x
    // kMaxRanks): a broken record, dropped on load
    inline constexpr float kMaxSavedXP = kMaxTierXP * kMaxRankMultiplier * kMaxRanks;

    struct Settings
    {
        bool enabled = kDefaultEnabled;
        int ranks = kDefaultRanks;
        std::array<float, kTierCount> tierXP = kDefaultTierXP;
        // One per rank from rank 1; a shorter list repeats its last value, an empty one is 1
        std::vector<float> rankMultipliers = std::vector<float>(kDefaultRanks, kDefaultRankMultiplier);
        float skillAtMin = kDefaultSkillAtMin;   // the XP factor at the tier's starting skill
        float skillAt100 = kDefaultSkillAt100;   // ... and at skill 100, straight line between, clamped
        float xpRate = kDefaultXPRate;
    };

    // Where a spell's XP stands: the rank reached, the ranks there are, and the XP the rank
    // started at and the next one starts at (both the total at the last rank)
    struct Progress
    {
        int rank = 0;
        int ranks = kDefaultRanks;
        float floor = 0.0f;
        float next = 0.0f;
    };

    // A tier name as DetermineSpellTier writes it ("Novice" ... "Master", any case) as an index.
    // Anything else is Novice, as ProgressionManager::GetXPForTier reads it.
    [[nodiscard]] inline std::size_t TierIndex(std::string_view tier) noexcept
    {
        for (std::size_t i = 0; i < kTierCount; ++i) {
            const auto key = kTierKeys[i];
            if (key.size() != tier.size()) continue;
            bool same = true;
            for (std::size_t c = 0; c < key.size() && same; ++c) {
                const char ch = tier[c];
                same = (ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch) == key[c];
            }
            if (same) return i;
        }
        return 0;
    }

    // Rank `rank`'s multiplier (rank 1 is index 0); past the list its last value
    [[nodiscard]] inline float RankMultiplier(const Settings& s, int rank) noexcept
    {
        if (s.rankMultipliers.empty() || rank < 1) return kDefaultRankMultiplier;
        const auto index = (std::min)(static_cast<std::size_t>(rank - 1), s.rankMultipliers.size() - 1);
        return s.rankMultipliers[index];
    }

    // The XP rank `rank` (1..ranks) costs on its own
    [[nodiscard]] inline float RankCost(const Settings& s, std::size_t tier, int rank) noexcept
    {
        return s.tierXP[(std::min)(tier, kTierCount - 1)] * RankMultiplier(s, rank);
    }

    // The XP a spell holds when it reaches `rank` (0 for rank 0; rank past `ranks` is the total)
    [[nodiscard]] inline float RankFloor(const Settings& s, std::size_t tier, int rank) noexcept
    {
        float total = 0.0f;
        const int last = (std::min)(rank, s.ranks);
        for (int r = 1; r <= last; ++r) total += RankCost(s, tier, r);
        return total;
    }

    [[nodiscard]] inline float TotalXP(const Settings& s, std::size_t tier) noexcept
    {
        return RankFloor(s, tier, s.ranks);
    }

    // The rank `xp` has reached: 0 for nothing, NaN or a negative value
    [[nodiscard]] inline int RankFor(float xp, const Settings& s, std::size_t tier) noexcept
    {
        if (!(xp > 0.0f)) return 0;
        int rank = 0;
        float floor = 0.0f;
        for (int r = 1; r <= s.ranks; ++r) {
            floor += RankCost(s, tier, r);
            if (xp < floor) break;
            rank = r;
        }
        return rank;
    }

    [[nodiscard]] inline Progress ProgressFor(float xp, const Settings& s, std::size_t tier) noexcept
    {
        Progress p;
        p.ranks = s.ranks;
        p.rank = RankFor(xp, s, tier);
        p.floor = RankFloor(s, tier, p.rank);
        p.next = RankFloor(s, tier, p.rank + 1);
        return p;
    }

    // The XP factor for the school's skill level: skillAtMin at the tier's starting skill, skillAt100
    // at 100, a straight line between, held at the ends (a skill past 100 counts as 100). A Master
    // spell starts at 100, so there is no line: skillAtMin below 100, skillAt100 at 100 and above.
    // NaN is the starting skill.
    [[nodiscard]] inline float SkillFactor(float skill, std::size_t tier, const Settings& s) noexcept
    {
        const float minSkill = kTierMinSkill[(std::min)(tier, kTierCount - 1)];
        const float span = kFullSkill - minSkill;
        if (!(span > 0.0f)) return skill >= kFullSkill ? s.skillAt100 : s.skillAtMin;   // NaN: atMin
        if (!(skill > minSkill)) return s.skillAtMin;
        const float t = (std::min)((skill - minSkill) / span, 1.0f);
        return s.skillAtMin + (s.skillAt100 - s.skillAtMin) * t;
    }

    // One cast's mastery XP: the cast's base XP (Magicka cost / 10, at least 1, SpellCastHandler)
    // x xpRate x the skill factor. No early-learning, tome or Spell Research multiplier.
    [[nodiscard]] inline float GainFor(float baseXP, float skill, std::size_t tier, const Settings& s) noexcept
    {
        if (!(baseXP > 0.0f)) return 0.0f;
        return baseXP * s.xpRate * SkillFactor(skill, tier, s);
    }

    // The XP after a gain: at most the total of every rank; a value already at or past it (the settings
    // lowered the bar) is kept as it is, never cut. A gain that is not positive changes nothing.
    [[nodiscard]] inline float Accumulate(float xp, float gain, float total) noexcept
    {
        const float held = xp > 0.0f ? xp : 0.0f;
        if (!(gain > 0.0f) || !(held < total)) return held;
        return (std::min)(held + gain, total);
    }

    // A saved value worth keeping: positive, finite and one the settings can reach
    [[nodiscard]] inline bool IsValidSavedXP(float xp) noexcept
    {
        return xp > 0.0f && xp <= kMaxSavedXP;   // NaN and infinity fail one of the two
    }
}
