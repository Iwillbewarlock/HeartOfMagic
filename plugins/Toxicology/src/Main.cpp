// =============================================================================
// Heart of Magic Librarian - Toxicology
// =============================================================================
// When a spell the librarian tags "poison" hits, applies the player's top
// favorited poison to the target. Data load: settings, the ESL's forms, the
// poison spell set and the tome at Elgrim's Elixirs (game/ToxData.h). The
// poison to copy and its icon keyword: game/ToxSelection.h.
// =============================================================================

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxSelection.h"

namespace
{
    // SKSE/Interfaces.h:453-459 (MessagingInterface::kPreLoadGame, kPostLoadGame, kNewGame, kDataLoaded).
    // All of them arrive on the game's main thread.
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            ToxGame::LoadSettings();
            ToxGame::LoadForms();
            ToxGame::LoadPoisonSpells();
            ToxGame::PutTomeInShop();
            ToxGame::RegisterMenuSink();
            FlushLog();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            // The keyword added to a static form stays in memory across loads (spec 12)
            ToxGame::ResetSelection();
            FlushLog();
            break;
        case SKSE::MessagingInterface::kNewGame:
            // No kPreLoadGame before a new game: take the keyword off the last game's poison here
            ToxGame::ResetSelection();
            [[fallthrough]];
        case SKSE::MessagingInterface::kPostLoadGame:
            // A full scan in game rewrites the catalog
            ToxGame::LoadPoisonSpells();
            // Puts the keyword back on the loaded game's poison (spec 5.5)
            ToxGame::OnSelectionMaybeChanged();
            FlushLog();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog("Heart of Magic Librarian - Toxicology " TOXICOLOGY_VERSION ": loaded");
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage)) {
        logger::error("Toxicology: SKSE messaging listener not registered - the addon does nothing");
    }
    return true;
}
