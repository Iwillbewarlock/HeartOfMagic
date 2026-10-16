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
#include "CopyRequest.h"
#include "CopySwap.h"
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
#include "HitEvents.h"
#include "HitEffectHook.h"
#include "ImpactSwap.h"
#include "Palette.h"
#include "ProjectileHook.h"
#include "Recolor.h"
#include "ScriptTags.h"
#include "SelfTest.h"
#include "Tester.h"
#include "MainThreadQueue.h"
#include "Watchdog.h"

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
        // v2 expansion C1: the closure from the original forms, the fingerprint, the request and the NIF dump. Its own
        // failures are caught inside; this catch is for the closure walk itself, so the hooks below are installed anyway
        try {
            BlackFlame::PrepareCopyRequest(BlackFlame::BuildFireClosure());
        } catch (const std::exception& e) {
            logger::error("BlackFlame: copy request: the fire closure failed: {}", e.what());
        }
        BlackFlame::LoadGreyMap();
        (void)BlackFlame::LoadPalette();
        BlackFlame::EffectEventSink::GetSingleton()->Register();
        BlackFlame::HitEventSink::GetSingleton()->Register();
        BlackFlame::StartTicker();
        BlackFlame::Watchdog::Start();  // C6: log flushed each second, STALL blocks
        // Installed after the fire sets are built: the hooks read them from any thread. Always installed
        // (v2): besides the runtime recolour (its own config keys, off by default) they tint lights, feed
        // the self-test records and open the explosion side of the impact window
        BlackFlame::InstallProjectileHooks();
        BlackFlame::InstallExplosionHook();
        BlackFlame::InstallHazardHook();
        BlackFlame::InstallHitEffectHooks();
        BlackFlame::InstallImpactSwap();
        BlackFlame::InstallCopySwaps();  // v2: after the copies are filled and the impact call sites (CopySwap.h)
        BlackFlame::InstallHazardSwap();
        BlackFlame::RunGreyCheck();
        BlackFlame::Tester::WriteStartup();  // every hook's state, the copies, the runtime (Tester.h)
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
            BlackFlame::Watchdog::NoteLoadStarted();
            TearDown("a save is being loaded");
            BlackFlame::SelfTest::OnPreLoadGame();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            BlackFlame::Watchdog::NoteLoadEnded();
            BlackFlame::RefreshCopies("save load");  // C5: copies that no longer match their source, re-filled
            BlackFlame::RetryNativeWrappers();
            BlackFlame::RequestRecomputeBuff("game loaded");
            // SKSE passes the load's success flag as the data pointer's value, dataLen 1 (SKSE PluginAPI.h:
            // "data contains a bool"); anything else is read as success
            BlackFlame::SelfTest::OnGameLoaded(a_msg.dataLen != 1 || reinterpret_cast<std::uintptr_t>(a_msg.data) != 0);
            FlushLog();
            break;
        case SKSE::MessagingInterface::kNewGame:
            BlackFlame::Watchdog::NoteLoadEnded();
            TearDown("a new game started");
            BlackFlame::RefreshCopies("new game");
            BlackFlame::SelfTest::OnNewGame();
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

    const char* MessageName(std::uint32_t a_type)
    {
        switch (a_type) {
        case SKSE::MessagingInterface::kPostLoad:
            return "PostLoad";
        case SKSE::MessagingInterface::kPostPostLoad:
            return "PostPostLoad";
        case SKSE::MessagingInterface::kPreLoadGame:
            return "PreLoadGame";
        case SKSE::MessagingInterface::kPostLoadGame:
            return "PostLoadGame";
        case SKSE::MessagingInterface::kSaveGame:
            return "SaveGame";
        case SKSE::MessagingInterface::kDeleteGame:
            return "DeleteGame";
        case SKSE::MessagingInterface::kInputLoaded:
            return "InputLoaded";
        case SKSE::MessagingInterface::kNewGame:
            return "NewGame";
        case SKSE::MessagingInterface::kDataLoaded:
            return "DataLoaded";
        default:
            return "other";
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }
        // Called from SKSE: nothing may throw out of here
        try {
            // C6b: every message we receive, with its thread, written through at once (a hang keeps it)
            logger::info("BlackFlame: SKSE message {} ({}) on thread {}", a_msg->type, MessageName(a_msg->type),
                REX::W32::GetCurrentThreadId());
            FlushLog();
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
    // The trampoline holds the impact swap's three call redirects (ImpactSwap.cpp, 14 bytes each), the two v2
    // GetImpactData redirects (14 each) and the two v2 prologue detours (a 14-byte jump plus a gateway of up
    // to 16 + 14 bytes each, CopySwap.h) and the main-thread queue's call (14, MainThreadQueue.h): 172 bytes at most;
    // the rest is headroom
    constexpr std::size_t kTrampolineSize = 512;
    SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = kTrampolineSize });
    // Info lines are buffered (plugins/Common.h): warnings and errors, game load, save, new game
    // and the self-test's end write them out
    SetupLog("Black Flame prototype (grey-black fire for the player's fire spells while stoked)");

    BlackFlame::LoadConfig();
    BlackFlame::MainQueue::Install();          // D1: our per-frame main-thread drain (MainThreadQueue.h)
    if (!BlackFlame::MainQueue::Installed()) {
        // E6: without the queue our tasks run on SKSE's job-thread drain: nothing there may change engine data
        BlackFlame::DisableEngineWrites("the main-thread queue is not installed");
    }
    BlackFlame::StampGameThreadFallback();     // without the queue an SKSE task stamps the game thread (GameThread.h)

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
