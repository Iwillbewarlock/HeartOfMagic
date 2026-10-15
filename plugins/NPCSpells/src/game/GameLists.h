#pragma once

// =============================================================================
// NPC SPELLS - THE ESL'S LEVELED SPELL LISTS
// =============================================================================

#include "core/NpcSpellsBuild.h"
#include "core/NpcSpellsNpc.h"
#include "game/GamePool.h"

#include <cstddef>
#include <vector>

namespace NpcSpellsGame
{
    struct EslRecords
    {
        bool loaded = false;
        std::vector<RE::TESLevSpell*> byRecord;  // index = slot * kMaxChunks + chunk; null when missing
        std::size_t missing = 0, wrongType = 0;
    };

    // TESDataHandler::LookupLoadedLightModByName + LookupForm, then As<TESLevSpell>
    EslRecords LookupEslRecords();

    // Writes entries into each ESL record present (game heap, never marked changed); returns the ListIndex for DecideNpc.
    // A list whose record is missing is counted in skippedNoRecord, one the game heap gave no entry array in skippedNoMemory,
    // one whose record already holds entries in skippedPrefilled (count not 0: not touched); all three are left out of the
    // index. An empty record gets a new array.
    NpcSpells::ListIndex FillLists(const std::vector<NpcSpells::BuiltList>& lists, const ResolvedPool& pool,
        const EslRecords& records, std::size_t& skippedNoRecord, std::size_t& skippedPrefilled, std::size_t& skippedNoMemory);
}
