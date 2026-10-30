#pragma once

#include "Common.h"

// =============================================================================
// SpellResearchPatch - the game side of the Spell Research registrations
//
// Spell Research (SR) fills its archetype FormLists only when the player
// presses Import in its MCM, and only with the spells its import quests name.
// After that Import finishes (its message box, or the mod event SR_ImportFinished)
// - and at every save load - the catalog spells SR
// does not know yet are added to its lists the way an SR import quest adds
// them (BGSListForm::AddForm, so the entries live in the save and SR's next
// Import clears them like its own). Spells an SR patch registered keep their
// labels. SpellResearch.h has the plan, docs/librarian/SPELL_RESEARCH.md why.
//
// Game thread only: the lists are the ones SR's scripts read.
// =============================================================================

namespace Librarian::SpellResearchPatch
{
    // kDataLoaded: with SpellResearch.esp loaded, reads the rules file and
    // starts listening for the end of an Import: the message box SR shows, or
    // the SKSE mod event "SR_ImportFinished" a build without that box sends
    // (Papyrus SendModEvent). Both run the same idempotent check.
    void Install();

    // kPostLoadGame: brings the loaded save's lists up to date.
    void OnGameLoaded();

    // kNewGame: the lists are the new game's now.
    void OnNewGame();

    // After a full scan rewrote the catalog (game thread): spells it now has
    // are registered at once, not at the next load.
    void OnCatalogChanged();

    // What the last run found, for the panel's Settings > Spell Tome Learning
    // line (onSpellResearchStatus): {"state": "notImported" | "off" | "noCatalog"
    // | "upToDate" | "added", "srSpells", "added", "reason", "time"}. "" when SR
    // is not loaded or nothing ran yet. Any thread.
    std::string StatusJson();

    // SR's tier tome lists for SpellTomes (books, spells; same index = one pair).
    enum class TomeListState
    {
        kNotInstalled,   // SpellResearch.esp not loaded, or its lists not found
        kNotReady,       // no Import finished yet, or one runs (the Reconcile rule)
        kReady
    };
    using TomeLists = std::array<std::pair<RE::BGSListForm*, RE::BGSListForm*>, 5>;
    // Game thread. `out` is filled only when kReady.
    TomeListState ReadTomeLists(TomeLists& out);
}
