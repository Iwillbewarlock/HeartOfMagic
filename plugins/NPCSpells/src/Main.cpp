// =============================================================================
// Heart of Magic - NPC Spells
// =============================================================================
// Fills HeartOfMagic_NPCSpells.esp's leveled spell lists from the Heart of
// Magic spell tree once the game data is loaded. No hooks, no Papyrus.
// A dev-only self test (game/GameSelfTest.h) runs after a save load when its file is present.
// =============================================================================

#include "Common.h"
#include "game/GameRun.h"
#include "game/GameSelfTest.h"

namespace
{
    // SKSE/Interfaces.h:453-459 (MessagingInterface::kPreLoadGame, kPostLoadGame, kSaveGame, kDataLoaded). Not on kNewGame:
    // the self test would spawn hostile NPCs into the opening scene. kPostLoadGame's data is not read (its meaning is
    // not documented in CommonLibSSE-NG's headers).
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            NpcSpellsGame::Run();
            FlushLog();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            NpcSpellsGame::OnSelfTestPreLoad();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            NpcSpellsGame::MaybeStartSelfTest();
            break;
        case SKSE::MessagingInterface::kSaveGame:
            NpcSpellsGame::OnSelfTestSave();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog("Heart of Magic - NPC Spells: fills the ESL's leveled spell lists from the tree at data load");
    SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
    return true;
}
