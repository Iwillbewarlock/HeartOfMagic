#pragma once

// =============================================================================
// NPC SPELLS - NPC RECORDS
// =============================================================================
// Every TESNPC read once (its template chain for class, race and factions),
// decided with DecideNpc, and given its lists with one AddLevSpells call. An
// NPC whose spell list comes from a template gets, after every holder is
// decided, the same lists as its holder (InheritDecision): the engine copied
// the holder's list into it while loading, before kDataLoaded. Who is written
// and how is PlanWrites' (core/NpcSpellsPlan.h), made after a memory audit of
// every record and before the first write.
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
        // eligible, attached, listsAttached, unresolvableTemplate, multiGroup: the NPCs that decide for themselves only (an
        // inheriting one, its holder resolved or not, never reaches DecideNpc)
        std::size_t scanned = 0, eligible = 0, attached = 0, listsAttached = 0, unresolvableTemplate = 0, multiGroup = 0, unresolvedRefs = 0;
        // NPCs written with their spell list holder's lists, and those lists; of them, the ones given new spell list data
        // (kCopiedTemplate and none: the engine copied an empty holder); NPCs that got them through spell list data shared with
        // a written record (nothing written to them), and of those the ones whose own race rule would have given them nothing
        std::size_t inheritedAttached = 0, inheritedLists = 0, inheritedCreated = 0, inheritedViaShared = 0, viaSharedDespiteOwnSkip = 0;
        // Every NPC record that inherits its spell list through a template link (not the player; a chain that cannot tell
        // its holder before spawn included), read at kDataLoaded: without kCopiedTemplate, split by a leveled link in its
        // chain; met before its holder in the form array; class, race or factions from another record than the holder;
        // spell list data (SpellData) shared with a record of its chain; a holder at the chain's end with the Spells flag
        // and no template (it keeps its own list)
        std::size_t inheriting = 0, notCopiedLeveledChain = 0, notCopiedPlainChain = 0, beforeHolder = 0, otherProvider = 0;
        std::size_t sharedSpellData = 0, holderFlagNoTemplate = 0;
        // The memory audit over every NPC record: records whose leveled-list array another record's distinct spell list data
        // also points at (expected 0), and the writes left out for it; spell list data that NPC records outside one template
        // chain share (expected 0: every record written through it adds its lists to it)
        std::size_t sharedLevSpells = 0, blockedWrites = 0, sharedSpellDataOutsideChain = 0;
        std::map<std::string, std::size_t> skipped;  // DecideNpc's and PlanWrites' skip reasons, plus "no source file" and "no race"
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
    // flag without a template link is `npc` itself (resolved), as AttachToNpcs reads it; on a template at the chain's end
    // with no template of its own, that template.
    RE::TESNPC* SpellListHolder(RE::TESNPC* npc, bool& unresolvable);

    // `npc` and the NPC records of its template chain, in order, up to the first link that is not an NPC (the walk SpellListHolder uses)
    std::vector<RE::TESNPC*> TemplateChain(RE::TESNPC* npc);
}
