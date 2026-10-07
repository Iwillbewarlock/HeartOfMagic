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
    inline constexpr std::size_t kSchoolCount = 5;
    inline constexpr std::size_t kTierCount = 5;
    inline constexpr std::array<std::uint16_t, kTierCount> kTierFloor{ 0, 25, 50, 75, 100 };
    inline constexpr std::size_t kMaxEntries = 255;   // TESLeveledList::numEntries is uint8
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
