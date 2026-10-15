// =============================================================================
// Heart of Magic - NPC Spells
// =============================================================================
// Fills HeartOfMagic_NPCSpells.esp's leveled spell lists from the Heart of
// Magic spell tree once the game data is loaded. No hooks, no Papyrus.
// A dev-only self test (game/GameSelfTest.h) runs after a load when its file is present.
// =============================================================================

#include "Common.h"
#include "game/GameRun.h"
#include "game/GameSelfTest.h"

namespace
{
    // SKSE/Interfaces.h:454-459 (MessagingInterface::kPostLoadGame, kNewGame, kDataLoaded)
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
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            NpcSpellsGame::MaybeStartSelfTest();
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
