#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
namespace NpcSpells {
    enum class School : std::uint8_t { Alteration = 0, Conjuration, Destruction, Illusion, Restoration, Count };
    enum class Tier : std::uint8_t { Novice = 0, Apprentice, Adept, Expert, Master, Count };
    enum class ClassTier : std::uint8_t { NonMage, Hybrid, Mage };
    enum class UndeadPolicy : std::uint8_t { NoUndead, Conjurer, Necromancer };
    inline constexpr std::size_t kClassTierCount = static_cast<std::size_t>(ClassTier::Mage) + 1;
    inline constexpr std::size_t kUndeadPolicyCount = static_cast<std::size_t>(UndeadPolicy::Necromancer) + 1;
    inline constexpr std::size_t kSchoolCount = 5;
    inline constexpr std::size_t kTierCount = 5;
    // Entry level per tier (the NPC's school skill must reach it). Novice is 1, not 0: in-game run 7 (2026-10-15) no list
    // whose highest level at or below the skill was 0 gave a spell, and no LVSP in the load order has a level 0 entry
    // (vanilla starts at 1), so the engine is taken to skip level 0 entries (ledger H7a)
    inline constexpr std::array<std::uint16_t, kTierCount> kTierFloor{ 1, 25, 50, 75, 100 };
    inline constexpr std::size_t kMaxEntries = 255;   // TESLeveledList::numEntries is uint8
    inline constexpr std::uint8_t kAllLevelsFlag = 1;  // llFlags bit 0: kCalculateFromAllLevelsLTOrEqPCLevel
    inline constexpr std::size_t kRungCount = 5;      // ladder length; the slot table has 5 rungs per (school, group, variant)
    inline constexpr std::array<std::string_view, kSchoolCount> kSchoolNames{ "alteration", "conjuration", "destruction", "illusion", "restoration" };
    inline constexpr std::array<std::string_view, kTierCount> kTierNames{ "novice", "apprentice", "adept", "expert", "master" };
    // inline: defined in the header, included by many TUs
    inline std::optional<School> SchoolFromName(std::string_view lowered) {
        for (std::size_t i = 0; i < kSchoolCount; ++i) if (kSchoolNames[i] == lowered) return static_cast<School>(i);
        return std::nullopt;
    }
    inline std::optional<Tier> TierFromName(std::string_view lowered) {
        for (std::size_t i = 0; i < kTierCount; ++i) if (kTierNames[i] == lowered) return static_cast<Tier>(i);
        return std::nullopt;
    }
}
