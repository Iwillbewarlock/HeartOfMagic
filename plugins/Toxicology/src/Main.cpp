// =============================================================================
// Heart of Magic Librarian - Toxicology
// =============================================================================
// When a spell the librarian tags "poison" hits, applies the player's top
// favorited poison to the target. Data load: settings, the ESL's forms, the
// poison spell set and the tome at Elgrim's Elixirs (game/ToxData.h). The
// poison to copy and its icon keyword: game/ToxSelection.h. Hits: game/ToxHits.h;
// applying the poison once per target: game/ToxApply.h. Magicka spent on poison
// spells uses up bottles: game/ToxMagicka.h, kept in the co-save: game/ToxSave.h.
// A dev-only self test (game/ToxSelfTest.h) runs after a save load when its file is present.
// =============================================================================

#include "Common.h"
#include "game/ToxApply.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"
#include "game/ToxMagicka.h"
#include "game/ToxSave.h"
#include "game/ToxSelection.h"
#include "game/ToxSelfTest.h"

namespace
{
    // SKSE/Interfaces.h:453-459 (MessagingInterface::kPreLoadGame, kPostLoadGame, kSaveGame, kNewGame,
    // kDataLoaded). All of them arrive on the game's main thread. kPreLoadGame's and kSaveGame's data is
    // the save name by skse64's convention (not documented in CommonLibSSE-NG); only the dev self test
    // reads it, as text of at most dataLen bytes.
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
            ToxGame::RegisterContainerSink();
            ToxGame::RegisterHitSink();
            ToxGame::InstallMagickaTracking();
            FlushLog();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            // Hit tasks queued before the load are dropped; each target can be poisoned once more (spec 5.4)
            ToxGame::BumpGeneration();
            ToxGame::ClearApplications();
            // The keyword added to a static form stays in memory across loads (spec 12)
            ToxGame::ResetSelection();
            ToxGame::OnSelfTestPreLoad(static_cast<const char*>(a_msg->data), a_msg->dataLen);
            FlushLog();
            break;
        case SKSE::MessagingInterface::kSaveGame:
            ToxGame::OnSelfTestSave(static_cast<const char*>(a_msg->data), a_msg->dataLen);
            break;
        case SKSE::MessagingInterface::kNewGame:
            ToxGame::OnSelfTestNewGame();
            ToxGame::BumpGeneration();
            ToxGame::ClearApplications();
            ToxGame::ResetMagicka();  // spec 5.6: a new game starts at 0 (co-save Revert does the same)
            // No kPreLoadGame before a new game: take the keyword off the last game's poison here
            ToxGame::ResetSelection();
            [[fallthrough]];
        case SKSE::MessagingInterface::kPostLoadGame:
            // A full scan in game rewrites the catalog: read again only when the file changed
            ToxGame::ReloadPoisonSpellsAtLoad();
            // Puts the keyword back on the loaded game's poison (spec 5.5)
            ToxGame::OnSelectionMaybeChanged();
            // ...and off any other poison the save brought it on (a player-made one keeps it in the save)
            ToxGame::SweepMimicKeyword();
            // Dev only, idle without its file; never on a new game (spawned targets in the opening scene)
            if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame) ToxGame::MaybeStartSelfTest();
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
    ToxGame::RegisterCoSave();
    return true;
}
