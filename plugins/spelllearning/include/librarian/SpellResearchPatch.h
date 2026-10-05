#pragma once

#include "Common.h"

// =============================================================================
// SpellResearchPatch - the game side of the Spell Research registrations
//
// Spell Research (SR) fills its archetype FormLists only when the player
// presses Import in its MCM, and only with the spells its import quests name.
// After that Import finishes - and at every save load - the catalog spells SR
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
    // starts listening for the message box SR shows when an Import finishes.
    void Install();

    // kPostLoadGame: brings the loaded save's lists up to date.
    void OnGameLoaded();

    // kNewGame: the lists are the new game's now.
    void OnNewGame();

    // After a full scan rewrote the catalog (game thread): spells it now has
    // are registered at once, not at the next load.
    void OnCatalogChanged();

    // The HUD line after spells were registered (always shown: it comes once per
    // Import), in the panel's language with {{count}} for the number (lang key
    // notifications.spellResearchAdded).
    inline constexpr const char* kAddedNoticeEnglish = "Heart of Magic: {{count}} mod spells added to Spell Research";
    void SetNoticeText(std::string text);
}
