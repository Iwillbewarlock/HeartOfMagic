// =============================================================================
// HeartOfMagic_BlackFlame - "흑염 달구기" prototype
// =============================================================================
// While the player's Stoke buff (HeartOfMagic_BlackFlame.esp) is active, the
// visuals of the player's own fire spells turn grey-black. Each layer has its
// own config switch (Config.h). See plugins/BlackFlame/README.md.
// =============================================================================

#include "Common.h"

#include "BuffState.h"
#include "Config.h"
#include "EffectEvents.h"
#include "ExplosionHook.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"
#include "Palette.h"
#include "ProjectileHook.h"

namespace
{
    void OnDataLoaded()
    {
        BlackFlame::MarkGameThread();
        if (!BlackFlame::LoadForms()) {
            logger::error("BlackFlame: the addon ESP's forms are missing - nothing is installed");
            return;
        }
        BlackFlame::BuildFireSets();
        (void)BlackFlame::LoadPalette();
        BlackFlame::EffectEventSink::GetSingleton()->Register();
        BlackFlame::StartTicker();
        // Installed after the fire sets are built: the hooks read them from any thread
        if (BlackFlame::GetConfig().projectile) {
            BlackFlame::InstallProjectileHooks();
        }
        if (BlackFlame::GetConfig().explosion) {
            BlackFlame::InstallExplosionHook();
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            BlackFlame::RecomputeBuff("game loaded");
            break;
        case SKSE::MessagingInterface::kNewGame:
            BlackFlame::RecomputeBuff("new game");
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    SetupLog("Black Flame prototype (grey-black fire for the player's fire spells while stoked)");
    // A prototype checked from its log during play: every info line goes to disk at once
    spdlog::flush_on(spdlog::level::info);

    BlackFlame::LoadConfig();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::error("BlackFlame: could not register the SKSE message listener - the addon stays off");
        return true;
    }
    logger::info("BlackFlame: loaded");
    return true;
}
