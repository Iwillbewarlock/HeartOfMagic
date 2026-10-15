#pragma once

// =============================================================================
// NPC SPELLS - NPC RECORDS
// =============================================================================
// Every TESNPC read once (its template chain for class, race and factions),
// decided with DecideNpc, and given its lists with one AddLevSpells call.
// =============================================================================

#include "core/NpcSpellsNpc.h"
#include "core/NpcSpellsRules.h"
#include "game/GameLists.h"

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace NpcSpellsGame
{
    struct NpcStats
    {
        std::size_t scanned = 0, eligible = 0, attached = 0, listsAttached = 0, unresolvableTemplate = 0, multiGroup = 0, unresolvedRefs = 0;
        std::map<std::string, std::size_t> skipped;  // DecideNpc's skip reasons, plus "no source file" and "no race"
        std::array<std::size_t, NpcSpells::kClassTierCount> byTier{};  // ClassTier of the NPCs that got lists
        std::array<std::size_t, NpcSpells::kGroupCount> byGroup{};     // kGroups index of the NPCs that got lists
    };

    // lines: one row per NPC that got at least one list (every one, no cap; the report lists them all)
    NpcStats AttachToNpcs(const NpcSpells::Rules& rules, const NpcSpells::ListIndex& index, const EslRecords& records,
        std::vector<NpcSpells::NpcLine>& lines);
}
