// =============================================================================
// HeartOfMagic_BlackFlame - "흑염 달구기" prototype
// =============================================================================
// While the player's Stoke buff (HeartOfMagic_BlackFlame.esp) is active, the
// visuals of the player's own fire spells turn grey-black. Each layer has its
// own config switch (Config.h). See plugins/BlackFlame/README.md.
// =============================================================================

#include "Common.h"

#include "BuffState.h"
#include "CasterArt.h"
#include "Config.h"
#include "EffectEvents.h"
#include "ExplosionHook.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "Forms.h"
#include "GameThread.h"
#include "GreyCheck.h"
#include "GreyMap.h"
#include "HazardHook.h"
#include "HazardSwap.h"
#include "PapyrusWrap.h"
#include "PlayerArt.h"
#include "HitArt.h"
#include "HitEffectHook.h"
#include "ImpactSwap.h"
#include "Palette.h"
#include "ProjectileHook.h"
#include "Recolor.h"
#include "ScriptTags.h"
#include "SelfTest.h"

namespace
{
    void OnDataLoaded()
    {
        BlackFlame::g_dataLoadedThreadNativeId.store(REX::W32::GetCurrentThreadId());
        if (!BlackFlame::LoadForms()) {
            logger::error("BlackFlame: the addon ESP's forms are missing - nothing is installed");
            return;
        }
        BlackFlame::LoadAndFillCopies();  // v2: before any hook reads the copies (FormCopies.h)
        BlackFlame::BuildFireSets();
        BlackFlame::LoadGreyMap();
        (void)BlackFlame::LoadPalette();
        BlackFlame::EffectEventSink::GetSingleton()->Register();
        BlackFlame::StartTicker();
        // Installed after the fire sets are built: the hooks read them from any thread. Always installed
        // (v2): besides the runtime recolour (its own config keys, off by default) they tint lights, feed
        // the self-test records and open the explosion side of the impact window
        BlackFlame::InstallProjectileHooks();
        BlackFlame::InstallExplosionHook();
        BlackFlame::InstallHazardHook();
        BlackFlame::InstallHitEffectHooks();
        BlackFlame::InstallImpactSwap();
        BlackFlame::InstallHazardSwap();
        BlackFlame::RunGreyCheck();
    }

    // The world is torn down: nothing tracked from it may be touched again
    void ResetLayers()
    {
        BlackFlame::g_worldGeneration.fetch_add(1);  // tasks queued in the old world now do nothing
        BlackFlame::ResetBuffFlag();
        BlackFlame::ResetHitEffectState();
        BlackFlame::ResetHitArt();
        BlackFlame::ResetHazardState();
        BlackFlame::ResetScriptTags();
        BlackFlame::ResetPlayerArt();
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
        constexpr int kMessageThreadLogs = 8;  // kDataLoaded, a load or two, saves
        BlackFlame::LogThread("SKSE message handler", "message", kMessageThreadLogs);
        switch (a_msg.type) {
        case SKSE::MessagingInterface::kPreLoadGame:
            TearDown("a save is being loaded");
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            BlackFlame::RetryNativeWrappers();
            BlackFlame::RequestRecomputeBuff("game loaded");
            BlackFlame::SelfTest::OnGameLoaded();
            FlushLog();
            break;
        case SKSE::MessagingInterface::kNewGame:
            TearDown("a new game started");
            BlackFlame::RetryNativeWrappers();
            BlackFlame::RequestRecomputeBuff("new game");
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
    // The trampoline holds the impact swap's three call redirects (ImpactSwap.cpp)
    constexpr std::size_t kTrampolineSize = 128;
    SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = kTrampolineSize });
    BlackFlame::StampGameThreadFromTask();  // the first SKSE task stamps the game thread (GameThread.h)
    // Info lines are buffered (plugins/Common.h): warnings and errors, game load, save, new game
    // and the self-test's end write them out
    SetupLog("Black Flame prototype (grey-black fire for the player's fire spells while stoked)");

    BlackFlame::LoadConfig();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::error("BlackFlame: could not register the SKSE message listener - the addon stays off");
        return true;
    }
    // Native wrappers bound when SKSE registers Papyrus functions (after the game's own natives)
    if (auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(BlackFlame::InstallNativeWrappers)) {
        logger::warn("BlackFlame: could not register with the SKSE Papyrus interface - script-placed fire is not tagged");
    }
    logger::info("BlackFlame: loaded");
    return true;
}
