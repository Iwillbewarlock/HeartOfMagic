#pragma once
#include "core/NpcSpellsPool.h"
#include "core/NpcSpellsSlots.h"
#include <array>
#include <cstdint>
#include <vector>

namespace NpcSpells {
    struct Entry { std::uint32_t spellIndex; std::uint16_t level; };        // index into the pool
    struct BuiltList { std::size_t slotIndex; std::uint8_t chunk; std::uint8_t flags; std::vector<Entry> entries; };  // flags: 0 or 1 (kCalculateFromAllLevelsLTOrEqPCLevel)
    struct BuildStats { std::size_t lists = 0, emptySlots = 0, droppedOverCapacity = 0; std::array<std::size_t, kSchoolCount> perSchool{}; };
    // Every (slot, chunk) with at least one entry. Entries sorted ascending by level, one school per list. A slot is split only when it
    // holds more than kMaxEntries spells (ceil(total / 255) chunks, at most kMaxChunks, fed round-robin across tiers); otherwise one list, chunk 0.
    std::vector<BuiltList> BuildLists(const std::vector<SpellFacts>& pool, const Rules& rules, BuildStats& stats);
    // Membership of one spell in one slot (public for the test): allowed = not in group never; variant rules of Task 4.
    bool InSlot(const SpellFacts& spell, const SlotKey& key, const Rules& rules);
}
