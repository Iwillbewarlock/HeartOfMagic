#pragma once
#include "core/NpcSpellsClassTier.h"
#include "core/NpcSpellsSlots.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace NpcSpells {
    struct NpcFacts {
        std::string pluginLower; std::uint32_t localId = 0;   // dice key
        bool inheritsSpellList = false;   // own kSpells flag
        bool unresolvableTraits = false, unresolvableStats = false, unresolvableFactions = false;
        bool isPlayer = false, isBeast = false, isCreature = false, isRedguard = false, isUndeadException = false, isNecromancerClass = false;
        bool hasClass = false; ClassWeights weights;
        std::vector<std::uint8_t> groupsMatched;   // built-in group indices, any order
    };
    struct ListIndex { std::vector<std::array<bool, kMaxChunks>> present; };   // [slot][chunk] exists and is non-empty
    struct Attachment { std::size_t slotIndex; std::uint8_t chunk; };
    struct NpcDecision { bool eligible = false; const char* skipReason = nullptr; ClassTier tier = ClassTier::NonMage; std::uint8_t group = kGroupNone; UndeadPolicy undead = UndeadPolicy::NoUndead; std::vector<Attachment> lists; std::size_t slotsWithoutList = 0; };
    // FNV-1a 64 of (plugin, local id, salt), run through MurmurHash3's fmix64 so salts that differ only in the last
    // byte (the rung digit) give independent values; top 53 bits -> [0,1)
    double Dice(std::string_view pluginLower, std::uint32_t localId, std::string_view salt);
    NpcDecision DecideNpc(const NpcFacts& npc, const Rules& rules, const ListIndex& lists);
    // One report row per NPC that got lists (Task 9 fills it, Task 10 renders it): persistentId "Plugin.esp|0x00XXXX" (catalog form), display name
    struct NpcLine { std::string persistentId; std::string name; NpcDecision decision; };
}
