// =============================================================================
// Heart of Magic Librarian - Toxicology
// =============================================================================
// When a spell the librarian tags "poison" hits, applies the player's top
// favorited poison to the target. Data load: settings, the ESL's forms, the
// poison spell set and the tome at Elgrim's Elixirs (game/ToxData.h).
// =============================================================================

#include "Common.h"
#include "game/ToxData.h"

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
            FlushLog();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            FlushLog();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            // A full scan in game rewrites the catalog
            ToxGame::LoadPoisonSpells();
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
