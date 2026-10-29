#pragma once

#include "SrBlockRules.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

// =============================================================================
// SR DATA - Spell Research's own lists in SpellResearch.esp, read only:
// _SR_ListSpellsArchetypes (60 lists of spells), _SR_ListAllSpells (5 tier lists),
// _SR_ListGlobalsArchetypes (60 lists of 5 knowledge globals), _SR_GlobalImporting
// and _SR_GlobalIsSetup. HasForm and the lists' arrays are written on the game
// thread (SR's Import, the Librarian's AddForm) without a lock, so they are never
// walked from draw: a snapshot is built in an SKSE task on the game thread when
// the magic menu opens and published as an immutable object (an atomic
// shared_ptr swap) that draw reads.
// =============================================================================

namespace SrData
{
    struct Snapshot
    {
        bool ready = false;       // SR set up and not importing (else nothing below is filled)
        bool listsEmpty = true;   // no spell in any archetype list (SR's Import never ran)
        std::unordered_map<std::uint32_t, SrBlock::SpellEntry> spells;  // by FormID
        SrBlock::Knowledge knowledge{};
        std::size_t archetypeEntries = 0;  // spell entries over the 60 archetype lists (record + script-added)
        std::size_t scriptAdded = 0;       // of them script-added (scriptAddedTempForms: the Librarian, SR's Import)
        std::uint32_t firstScriptAdded = 0;       // one such spell and an archetype list it is in (self-test)
        int firstScriptAddedArchetype = -1;
    };

    /// Looks up SR's forms in SpellResearch.esp and checks the lists' shape (kDataLoaded, game data loaded). False, logged,
    /// when the plugin or a form is missing or a list has the wrong shape: the block is not registered then
    bool Init();

    /// Queues a snapshot build on the game thread (the magic menu opened). a_onBuilt runs in that task after the new snapshot
    /// is published
    void RequestBuild(void (*a_onBuilt)());

    /// The last published snapshot (null before the first build). Any thread
    std::shared_ptr<const Snapshot> Current();
}
