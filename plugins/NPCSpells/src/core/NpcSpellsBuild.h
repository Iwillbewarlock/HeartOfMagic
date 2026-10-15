#pragma once
#include "core/NpcSpellsPool.h"
#include "core/NpcSpellsSlots.h"
#include <array>
#include <cstdint>
#include <vector>

namespace NpcSpells {
    struct Entry { std::uint32_t spellIndex; std::uint16_t level; };        // index into the pool
    struct BuiltList { std::size_t slotIndex; std::uint8_t chunk; std::uint8_t flags; std::vector<Entry> entries; };  // flags: 0 or 1 (kCalculateFromAllLevelsLTOrEqPCLevel)
    struct BuildStats { std::size_t lists = 0, emptySlots = 0, droppedOverCapacity = 0, variantsToppedUp = 0; std::array<std::size_t, kSchoolCount> perSchool{}; };
    // Every (slot, chunk) with at least one entry. Entries sorted ascending by level, one school per list. A slot is split only when it
    // holds more than kMaxEntries spells; otherwise one list, chunk 0. A split slot takes the smallest chunk count c from ceil(total / 255)
    // with ceil(rest / c) + thin <= 255 (thin: the spells of tiers with fewer than c spells, copied into every chunk first; rest:
    // the others, fed round-robin), at most kMaxChunks; when even kMaxChunks does not fit, the surplus is counted in droppedOverCapacity.
    // A variant slot (Prefer, NecroPrefer, NoUndeadAll) missing a tier its All slot holds takes that tier's All spells,
    // minus what the variant never holds (undead-rule spells in NoUndeadAll and a Mixed group's conjuration Prefer); counted in variantsToppedUp.
    std::vector<BuiltList> BuildLists(const std::vector<SpellFacts>& pool, const Rules& rules, BuildStats& stats);
    // Membership of one spell in one slot (public for the test): not in the group's never tags, in the rung's tiers, the variant's rule.
    bool InSlot(const SpellFacts& spell, const SlotKey& key, const Rules& rules);
}
