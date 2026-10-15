// =============================================================================
// Heart of Magic - NPC Spells
// =============================================================================
// Fills HeartOfMagic_NPCSpells.esp's leveled spell lists from the Heart of
// Magic spell tree once the game data is loaded. No hooks, no Papyrus.
// =============================================================================

#include "Common.h"
#include "game/GameRun.h"

namespace
{
    // SKSE/Interfaces.h:459 (MessagingInterface::kDataLoaded)
    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            NpcSpellsGame::Run();
            FlushLog();
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
