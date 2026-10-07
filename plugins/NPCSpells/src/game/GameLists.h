#pragma once

// =============================================================================
// NPC SPELLS - THE ESL'S LEVELED SPELL LISTS
// =============================================================================

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
}
