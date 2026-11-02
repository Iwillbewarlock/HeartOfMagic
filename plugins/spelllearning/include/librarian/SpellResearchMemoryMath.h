#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include <nlohmann/json.hpp>

// =============================================================================
// SpellResearchMemoryMath - practice and memory, the numbers
//
// Heart of Magic's casting XP is practice; Spell Research's archetype XP (60
// archetypes x 5 ranks of GlobalVariable) is memory. A. every 15% of practice
// a cast crosses (and 100%) pays one short research session into the spell's
// archetypes, through SR's own research gain table; B. the share of the spell's
// archetypes already remembered at its rank raises its casting XP.
// Design: docs/superpowers/specs/2026-11-02-sr-memory-exchange-design.md.
//
// Pure (no game types): the game side (SpellResearchMemory.h) and
// tools/progression-check --self-test run the same code. Every function is O(1).
// =============================================================================

namespace Librarian::SpellResearchMemoryMath
{
    // SR's ranks (index 0 Novice ... 4 Master), as its tier lists and each archetype's globals
    inline constexpr std::size_t kRanks = 5;

    // Progress is cut at every 15% (15, 30, ... 90) and at 100%: 7 boundaries from 0 to mastery
    inline constexpr int kChunkPercent = 15;
    inline constexpr int kLastChunkPercent = 90;
    inline constexpr int kFullPercent = 100;
    inline constexpr std::size_t kBoundaryCount = kLastChunkPercent / kChunkPercent + 1;

    // SR's research loop: slots s = 0..6, target rank t = R + 2 - s (_SR_EffectResearchScript.UpdateArchetypeList)
    inline constexpr int kSlots = 7;
    inline constexpr int kSlotRankOffset = 2;
    inline constexpr double kExponentDivisor = 3.0;

    // SR's ModGlobalVariable: adds only while a value is below this, and clamps to it
    inline constexpr float kGlobalCap = 10'000'000.0f;

    inline constexpr double kMinutesPerHour = 60.0;
    inline constexpr float kPercentToFraction = 0.01f;

    // Settings (config.json "spellResearch": { "memoryExchange": {...} })
    inline constexpr bool kDefaultPracticeToMemory = true;
    inline constexpr float kDefaultMemoryMinutes = 25.0f;
    inline constexpr float kMaxMemoryMinutes = 240.0f;
    inline constexpr bool kDefaultMemoryToPractice = true;
    inline constexpr float kDefaultMaxPracticeBoost = 25.0f;   // percent: the size of the tome inventory boost
    inline constexpr float kMaxPracticeBoost = 100.0f;

    // The boundaries as fractions of the required XP. Each is k / 100 (not k * 0.01f),
    // so a progress of exactly 0.15f sits on the 15% boundary.
    [[nodiscard]] inline std::array<float, kBoundaryCount> Boundaries() noexcept
    {
        std::array<float, kBoundaryCount> result{};
        for (std::size_t i = 0; i + 1 < kBoundaryCount; ++i) {
            result[i] = static_cast<float>((static_cast<int>(i) + 1) * kChunkPercent) / 100.0f;
        }
        result[kBoundaryCount - 1] = static_cast<float>(kFullPercent) / 100.0f;
        return result;
    }

    // How many boundaries lie in (oldProgress, newProgress]: one payment each. 0 when the
    // progress did not rise, or either end is NaN or negative.
    [[nodiscard]] inline int BoundariesCrossed(float oldProgress, float newProgress) noexcept
    {
        if (!(oldProgress >= 0.0f) || !(newProgress > oldProgress)) return 0;   // NaN fails both
        int crossed = 0;
        for (const float boundary : Boundaries()) {
            if (oldProgress < boundary && boundary <= newProgress) ++crossed;
        }
        return crossed;
    }

    // One research session's value: SR's _SR_GlobalResearchTimeBase (15.0) x hours.
    // 0 for anything not positive (memoryMinutes 0 turns the payment into nothing).
    [[nodiscard]] inline float ResearchValue(float timeBase, float memoryMinutes) noexcept
    {
        const double value = static_cast<double>(timeBase) * static_cast<double>(memoryMinutes) / kMinutesPerHour;
        return value > 0.0 ? static_cast<float>(value) : 0.0f;   // NaN too
    }

    // SR's research gain for a spell of rank R and value v, by target rank: gain[t] =
    // v ^ ((s + R + 1) / 3) with s = R + 2 - t, for the slots s = 0..6 whose t is 0..4.
    // 0 for a rank no slot reaches (R = 0 pays Novice to Adept only), and for v <= 0.
    [[nodiscard]] inline std::array<float, kRanks> GainTable(std::size_t rank, float value) noexcept
    {
        std::array<float, kRanks> gains{};
        if (rank >= kRanks || !(value > 0.0f)) return gains;
        const int r = static_cast<int>(rank);
        for (int slot = 0; slot < kSlots; ++slot) {
            const int target = r + kSlotRankOffset - slot;
            if (target < 0 || target >= static_cast<int>(kRanks)) continue;
            const double exponent = static_cast<double>(slot + r + 1) / kExponentDivisor;
            gains[static_cast<std::size_t>(target)] = static_cast<float>(std::pow(static_cast<double>(value), exponent));
        }
        return gains;
    }

    // SR's ModGlobalVariable: a value at or over the cap is left alone; otherwise the gain
    // is added and the sum clamped to the cap. A gain that is not positive changes nothing.
    [[nodiscard]] inline float ApplyGain(float current, float gain) noexcept
    {
        if (!(gain > 0.0f) || !(current < kGlobalCap)) return current;
        const float next = current + gain;
        return next > kGlobalCap ? kGlobalCap : next;
    }

    // The share of a spell's archetypes remembered at its rank (SR's MCM: known = value > 0)
    [[nodiscard]] inline float KnownShare(std::size_t known, std::size_t archetypes) noexcept
    {
        if (archetypes == 0) return 0.0f;
        if (known >= archetypes) return 1.0f;
        return static_cast<float>(known) / static_cast<float>(archetypes);
    }

    // B: casting XP x (1 + maxPracticeBoost% x known share); 1 with nothing known or no boost
    [[nodiscard]] inline float PracticeMultiplier(float maxBoostPercent, float knownShare) noexcept
    {
        if (!(maxBoostPercent > 0.0f) || !(knownShare > 0.0f)) return 1.0f;
        const float boost = (maxBoostPercent < kMaxPracticeBoost ? maxBoostPercent : kMaxPracticeBoost) * kPercentToFraction;
        const float share = knownShare < 1.0f ? knownShare : 1.0f;
        return 1.0f + boost * share;
    }

    struct Settings
    {
        bool practiceToMemory = kDefaultPracticeToMemory;
        float memoryMinutes = kDefaultMemoryMinutes;
        bool memoryToPractice = kDefaultMemoryToPractice;
        float maxPracticeBoost = kDefaultMaxPracticeBoost;   // percent
    };

    // config.json's "spellResearch" section's "memoryExchange": a missing section, key or a
    // value of the wrong type is the default; the numbers are clamped (minutes 0..240, boost 0..100)
    [[nodiscard]] inline Settings ReadSettings(const nlohmann::json& config)
    {
        Settings settings;
        if (!config.is_object()) return settings;
        const auto section = config.find("spellResearch");
        if (section == config.end() || !section->is_object()) return settings;
        const auto exchange = section->find("memoryExchange");
        if (exchange == section->end() || !exchange->is_object()) return settings;

        const auto flag = [&](const char* key, bool fallback) {
            const auto found = exchange->find(key);
            return (found != exchange->end() && found->is_boolean()) ? found->get<bool>() : fallback;
        };
        const auto number = [&](const char* key, float fallback, float most) {
            const auto found = exchange->find(key);
            if (found == exchange->end() || !found->is_number()) return fallback;
            const double value = found->get<double>();
            if (!(value > 0.0)) return 0.0f;
            return value > most ? most : static_cast<float>(value);
        };
        settings.practiceToMemory = flag("practiceToMemory", kDefaultPracticeToMemory);
        settings.memoryMinutes = number("memoryMinutes", kDefaultMemoryMinutes, kMaxMemoryMinutes);
        settings.memoryToPractice = flag("memoryToPractice", kDefaultMemoryToPractice);
        settings.maxPracticeBoost = number("maxPracticeBoost", kDefaultMaxPracticeBoost, kMaxPracticeBoost);
        return settings;
    }
}
