#pragma once

// =============================================================================
// NPC SPELLS - NPC RECORDS
// =============================================================================
// Every TESNPC read once (its template chain for class, race and factions),
// decided with DecideNpc, and given its lists with one AddLevSpells call. An
// NPC whose spell list comes from a template gets, after every holder is
// decided, the same lists as its holder (InheritDecision): the engine copied
// the holder's list into it while loading, before kDataLoaded.
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
        std::size_t inheritedAttached = 0, inheritedLists = 0;  // NPCs that got their spell list holder's lists, and those lists
        // Every NPC record that inherits its spell list (not the player), read at kDataLoaded: without kCopiedTemplate, split by
        // a leveled link in its chain; met before its holder in the form array; class, race or factions from another record
        // than the holder; spell list data (SpellData) or only its leveled-list array shared with a chain record (expected 0)
        std::size_t inheriting = 0, notCopiedLeveledChain = 0, notCopiedPlainChain = 0, beforeHolder = 0, otherProvider = 0;
        std::size_t sharedSpellData = 0, sharedLevSpells = 0;
        std::map<std::string, std::size_t> skipped;  // DecideNpc's skip reasons, plus "no source file" and "no race"
        std::array<std::size_t, NpcSpells::kClassTierCount> byTier{};  // ClassTier of the NPCs that got lists
        std::array<std::size_t, NpcSpells::kGroupCount> byGroup{};     // kGroups index of the NPCs that got lists
    };

    // lines: one row per NPC that got at least one list (every one, no cap; the report lists them all): the own decisions
    // in scan order, then the inherited ones (NpcLine::inheritedFrom set)
    NpcStats AttachToNpcs(const NpcSpells::Rules& rules, const NpcSpells::ListIndex& index, const EslRecords& records,
        std::vector<NpcSpells::NpcLine>& lines);

    // The record whose spell list `npc` really uses: its template chain's Spells provider, the same walk AttachToNpcs reads
    // classes, races and factions with (AttachToNpcs decides only for NPCs that are their own provider; one that inherits gets
    // its holder's lists). When a leveled
    // link comes first the chain cannot tell before spawn: `unresolvable` is set and `npc` itself is returned. The Spells
    // flag without a template link is `npc` itself (resolved), as AttachToNpcs reads it.
    RE::TESNPC* SpellListHolder(RE::TESNPC* npc, bool& unresolvable);

    // `npc` and the NPC records of its template chain, in order, up to the first link that is not an NPC (the walk SpellListHolder uses)
    std::vector<RE::TESNPC*> TemplateChain(RE::TESNPC* npc);
}
