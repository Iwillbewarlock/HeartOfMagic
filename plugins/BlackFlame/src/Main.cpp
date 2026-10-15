// =============================================================================
// HeartOfMagic_BlackFlame - "흑염 달구기" prototype
// =============================================================================
// While the player's Stoke buff (HeartOfMagic_BlackFlame.esp) is active, the
// visuals of the player's own fire spells turn grey-black. Each layer has its
// own config switch (Config.h). See plugins/BlackFlame/README.md.
// =============================================================================

#include "Common.h"

#include "BuffState.h"
#include "BurnShader.h"
#include "CasterArt.h"
#include "Config.h"
#include "EffectEvents.h"
#include "ExplosionHook.h"
#include "FireSet.h"
#include "Forms.h"
#include "GameThread.h"
#include "Palette.h"
#include "ProjectileHook.h"
#include "Recolor.h"
#include "SelfTest.h"

namespace
{
    void OnDataLoaded()
    {
        BlackFlame::StampGameThread();
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

    // The world is torn down: nothing tracked from it may be touched again
    void ResetLayers()
    {
        BlackFlame::g_worldGeneration.fetch_add(1);  // tasks queued in the old world now do nothing
        BlackFlame::ResetBurnState();
        BlackFlame::ResetExplosionState();
        BlackFlame::ResetCasterArt();
        BlackFlame::ResetRecolorState();
    }

    // Layers first, then the self-test abort (which does not touch the old handles either); each in
    // its own guard so a throw in one cannot skip the other
    void TearDown(const char* a_reason)
    {
        try {
            ResetLayers();
        } catch (...) {
            logger::error("BlackFlame: resetting the layers threw ({})", a_reason);
        }
        try {
            BlackFlame::SelfTest::Abort(a_reason);
        } catch (...) {
            logger::error("BlackFlame: the self-test abort threw ({})", a_reason);
        }
    }

    void HandleMessage(const SKSE::MessagingInterface::Message& a_msg)
    {
        switch (a_msg.type) {
        case SKSE::MessagingInterface::kPreLoadGame:
            TearDown("a save is being loaded");
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            BlackFlame::RecomputeBuff("game loaded");
            BlackFlame::SelfTest::OnGameLoaded();
            FlushLog();
            break;
        case SKSE::MessagingInterface::kNewGame:
            TearDown("a new game started");
            BlackFlame::RecomputeBuff("new game");
            FlushLog();
            break;
        case SKSE::MessagingInterface::kSaveGame:
            FlushLog();
            break;
        default:
            break;
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        // Called from SKSE: nothing may throw out of here
        try {
            HandleMessage(*a_msg);
        } catch (const std::exception& e) {
            logger::error("BlackFlame: SKSE message {} handler threw: {}", a_msg->type, e.what());
        } catch (...) {
            logger::error("BlackFlame: SKSE message {} handler threw an unknown exception", a_msg->type);
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    BlackFlame::g_loadThreadNativeId.store(REX::W32::GetCurrentThreadId());
    // Info lines are buffered (plugins/Common.h): warnings and errors, game load, save, new game
    // and the self-test's end write them out
    SetupLog("Black Flame prototype (grey-black fire for the player's fire spells while stoked)");

    BlackFlame::LoadConfig();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::error("BlackFlame: could not register the SKSE message listener - the addon stays off");
        return true;
    }
    logger::info("BlackFlame: loaded");
    return true;
}
