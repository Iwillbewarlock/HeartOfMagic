// =============================================================================
// HeartOfMagic_BlackFlame - "갈망" (Longing) prototype
// =============================================================================
// While the player's buff "갈망" (Longing: the stoke spell or the worn lantern,
// HeartOfMagic_BlackFlame.esp) is active, the visuals of the player's own fire
// spells turn grey-black. Each layer has its own config switch (Config.h). See
// plugins/BlackFlame/README.md.
// =============================================================================

#include "Common.h"

#include "BuffState.h"
#include "CatalogStartup.h"
#include "CloneWatch.h"
#include "CopyModelProbe.h"
#include "CasterArt.h"
#include "Config.h"
#include "CopyFreshness.h"
#include "CopyRequest.h"
#include "CopySwap.h"
#include "DevAutomation.h"
#include "EffectEvents.h"
#include "ExplosionHook.h"
#include "FileIndex.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "Forms.h"
#include "GameThread.h"
#include "HandArtRefresh.h"
#include "GreyCheck.h"
#include "GreyMap.h"
#include "HazardHook.h"
#include "HazardSwap.h"
#include "PapyrusWrap.h"
#include "PlayerArt.h"
#include "HitArt.h"
#include "HitEvents.h"
#include "HitEffectHook.h"
#include "HitPathAttribution.h"
#include "HitPathObserve.h"
#include "LanternDamage.h"
#include "LanternIed.h"
#include "LanternPlace.h"
#include "CorpseMark.h"
#include "MagicApplySink.h"
#include "SreProbe.h"
#include "ImpactSwap.h"
#include "Palette.h"
#include "ProjectileHook.h"
#include "Recolor.h"
#include "ScriptTags.h"
#include "SelfTest.h"
#include "SingleLaunch.h"
#include "SlotLedger.h"
#include "Tester.h"
#include "MainThreadQueue.h"
#include "MenuWatch.h"
#include "Watchdog.h"

namespace
{
    std::atomic<bool> g_runtimeReady{ false };

    void InstallRuntime()
    {
        // W23: every step before the hook installs runs guarded, so a throw in one cannot skip the installs below
        const auto guarded = [](const char* a_step, auto&& a_work) {
            try {
                a_work();
            } catch (const std::exception& e) {
                logger::error("BlackFlame: kDataLoaded: {} threw ({}) - going on to the hook installs", a_step, e.what());
            } catch (...) {
                logger::error("BlackFlame: kDataLoaded: {} threw an exception of an unknown type - going on to the hook installs",
                    a_step);
            }
        };
        const bool singleLaunch = BlackFlame::GetConfig().singleLaunch;
        if (!singleLaunch) {
            guarded("LoadAndFillCopies", [] { BlackFlame::LoadAndFillCopies(); });
        }
        guarded("BuildFireSets", [] { BlackFlame::BuildFireSets(); });
        // v2 expansion C1: the closure from the original forms, the fingerprint, the request and the NIF dump. Its own
        // failures are caught inside; this catch is for the closure walk itself, so the hooks below are installed anyway
        try {
            const auto closure = BlackFlame::BuildFireClosure();
            if (!singleLaunch) {
                BlackFlame::PrepareCopyRequest(closure);  // single launch: request v5 and the dump (CopyRequestV5.h)
            }
            if (singleLaunch) {
                // the slots are filled from the live closure every start: no copies map to be stale
                BlackFlame::MarkFreshnessUnchecked("single launch: the copies are the slot pool (no copies map)");
                guarded("SingleLaunch::OnDataLoaded", [&] { BlackFlame::SingleLaunch::OnDataLoaded(closure); });
            } else {
                BlackFlame::CheckFreshness(closure);  // C3: fresh / stale / none / pluginMissing / notApplied / texturesMissing (CopyFreshness.h)
            }
        } catch (const std::exception& e) {
            logger::error("BlackFlame: copy request: the fire closure failed: {}", e.what());
            BlackFlame::MarkFreshnessUnchecked(std::string("the fire closure failed: ") + e.what());
        }
        guarded("LoadGreyMap", [] { BlackFlame::LoadGreyMap(); });
        guarded("LoadPalette", [] { (void)BlackFlame::LoadPalette(); });
        if (!singleLaunch) {
            // v2i: every copy NIF demanded once on the main thread; failures out of the swaps (single launch: CopyGroups
            // verifies before it opens)
            guarded("QueueCopyModelProbe", [] { BlackFlame::QueueCopyModelProbe(); });
        }
        // The lantern (README "Lantern"): after BuildFireSets (the hazard spells read IsFireHazard), before the hit sink
        guarded("LanternDamage::Init", [] { BlackFlame::LanternDamage::Init(); });
        guarded("InitLanternPlace", [] { BlackFlame::InitLanternPlace(); });
        // before any save loads: the ARMA's models swapped while no lantern 3D exists (LanternIed.h)
        guarded("LanternIed::Init", [] { BlackFlame::LanternIed::Init(); });
        guarded("event sinks", [] {
            BlackFlame::EffectEventSink::GetSingleton()->Register();
            BlackFlame::HitEventSink::GetSingleton()->Register();
        });
        guarded("StartTicker", [] { BlackFlame::StartTicker(); });
        guarded("Watchdog::Start", [] { BlackFlame::Watchdog::Start(); });  // C6: log flushed each second, STALL blocks
        // Installed after the fire sets are built: the hooks read them from any thread. Always installed
        // (v2): besides the runtime recolour (its own config keys, off by default) they tint lights, feed
        // the self-test records and open the explosion side of the impact window
        // X15: each one on its own, so a throw in one does not skip the rest
        guarded("InstallProjectileHooks", [] { BlackFlame::InstallProjectileHooks(); });
        guarded("InstallExplosionHook", [] { BlackFlame::InstallExplosionHook(); });
        guarded("InstallHazardHook", [] { BlackFlame::InstallHazardHook(); });
        guarded("InstallHitEffectHooks", [] { BlackFlame::InstallHitEffectHooks(); });
        guarded("HitPath::Install", [] { BlackFlame::HitPath::Install(); });  // log only, 1.6.1170 / 1.7.104 only (HitPathObserve.h)
        // corpse mark only: the elemental hits before FEC's Papyrus handlers run (MagicApplySink.h); after BuildFireSets (it
        // reads the fire sets) and HitPath::Install (added only when the corpse mark's hook was written)
        guarded("RegisterMagicApplySink", [] { BlackFlame::HitPath::RegisterMagicApplySink(); });
        // log only, any runtime: the fire MGEFs' shader fields now, and again when the main menu first opens (MenuWatch's
        // listener, on the menu event's thread), at each kPostLoadGame and the first stoke window (HitEffectHook)
        guarded("hit shader probe", [] {
            // inside our own kDataLoaded handler: the plugins whose handlers run after ours have not changed anything yet
            BlackFlame::HitPath::LogHitShaderProbe("kDataLoaded, inside BlackFlame's handler");
            BlackFlame::MenuWatch::OnMainMenuOpen([] { BlackFlame::HitPath::LogHitShaderProbe("main menu"); });
        });
        // log only, 1.6.1170 / 1.7.104 (armed by HitPath::Install): the SRE vtable again and the GetTexture probe (SreProbe.h)
        guarded("SRE probes", [] { BlackFlame::MenuWatch::OnMainMenuOpen([] { BlackFlame::HitPath::RunMainMenuProbes(); }); });
        guarded("InstallImpactSwap", [] { BlackFlame::InstallImpactSwap(); });
        guarded("InstallCopySwaps", [] { BlackFlame::InstallCopySwaps(); });  // v2: after the copies are filled (CopySwap.h)
        guarded("InstallHazardSwap", [] { BlackFlame::InstallHazardSwap(); });
        guarded("RunGreyCheck", [] { BlackFlame::RunGreyCheck(); });
        guarded("Tester::WriteStartup", [] { BlackFlame::Tester::WriteStartup(); });  // every hook's state, the copies
        guarded("DevAutomation::Install", [] { BlackFlame::DevAutomation::Install(); });  // dev only (DevAutomation.h)
    }

    void OnDataLoaded()
    {
        BlackFlame::g_dataLoadedThreadNativeId.store(REX::W32::GetCurrentThreadId());
        // W23: every step before the hook installs runs guarded, so a throw in one cannot skip the installs below
        const auto guarded = [](const char* a_step, auto&& a_work) {
            try {
                a_work();
            } catch (const std::exception& e) {
                logger::error("BlackFlame: kDataLoaded: {} threw ({}) - going on to the hook installs", a_step, e.what());
            } catch (...) {
                logger::error("BlackFlame: kDataLoaded: {} threw an exception of an unknown type - going on to the hook installs",
                    a_step);
            }
        };
        // Single launch: the main-thread proof and the menu sink before anything is scheduled (MenuWatch.h); always (the dev
        // auto-load reads the main menu from it), guarded too (review Gemini / agy 10)
        guarded("MenuWatch::Install", [] { BlackFlame::MenuWatch::Install(); });
        if (!BlackFlame::LoadForms()) {
            logger::error("BlackFlame: the addon ESP's forms are missing - nothing is installed");
            BlackFlame::MarkFreshnessUnchecked("the addon ESP's forms are missing");
            return;
        }
        const bool singleLaunch = BlackFlame::GetConfig().singleLaunch;
        if (singleLaunch) {
            // Single launch: the engine's resource index first (RegisterLoose is gated on its structural checks), then the
            // slot ledger (no ledger path: no slots this session). The copies map route below does not run (SingleLaunch.h)
            guarded("FileIndex::Init", [] { BlackFlame::FileIndex::Init(); });
            guarded("SlotLedger::Load", [] { (void)BlackFlame::SlotLedger::Load(); });
        }
        BlackFlame::CatalogStartup::Begin([] {
            InstallRuntime();
            g_runtimeReady.store(true);
        });
    }

    // The world is torn down: nothing tracked from it may be touched again
    void ResetLayers()
    {
        BlackFlame::g_worldGeneration.fetch_add(1);  // tasks queued in the old world now do nothing
        BlackFlame::ResetBuffFlag();
        BlackFlame::ResetHitEffectState();
        BlackFlame::ResetHitArt();
        BlackFlame::ResetCloneWatch();
        BlackFlame::ResetHazardState();
        BlackFlame::ResetScriptTags();
        BlackFlame::ResetPlayerArt();
        BlackFlame::ResetExplosionState();
        BlackFlame::ResetCasterArt();
        BlackFlame::ResetHandArtRefresh();
        BlackFlame::ResetRecolorState();
        BlackFlame::LanternDamage::Reset();  // the placement's pending refs are dropped by the generation above
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
            BlackFlame::MenuWatch::Close("kPreLoadGame (a save is being loaded)");
            if (BlackFlame::GetConfig().singleLaunch) {
                BlackFlame::SlotLedger::OnPreLoadGame(a_msg.data, a_msg.dataLen);  // before the engine reads the save
            }
            BlackFlame::Watchdog::NoteLoadStarted();
            TearDown("a save is being loaded");
            BlackFlame::SelfTest::OnPreLoadGame();
            BlackFlame::DevAutomation::OnPreLoadGame();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame: {
            // SKSE passes the load's success flag as the data pointer's value, dataLen 1 (SKSE PluginAPI.h:
            // "data contains a bool"); anything else is read as success
            const bool loaded = a_msg.dataLen != 1 || reinterpret_cast<std::uintptr_t>(a_msg.data) != 0;
            BlackFlame::Watchdog::NoteLoadEnded();
            if (!g_runtimeReady.load()) {
                logger::warn("BlackFlame: runtime initialization is not ready; skipping save-load runtime work");
                FlushLog();
                break;
            }
            BlackFlame::RefreshCopies("save load");  // C5: copies that no longer match their source, re-filled
            BlackFlame::NotifyFreshnessOnce();
            BlackFlame::NotifyFileIndexOnce();  // a loose-file cache that does not know the copies (CopyModelProbe)
            BlackFlame::RetryNativeWrappers();
            BlackFlame::Tester::Write("save load");  // G5: the wrappers have bound by now
            BlackFlame::RequestRecomputeBuff("game loaded");
            BlackFlame::OnLanternGameLoaded();  // the player's cell, once the lab's 3D is there
            BlackFlame::LanternIed::OnGameReady("the game load", loaded);  // the player's IED entry (LanternIed.h)
            (void)BlackFlame::PlayerHandleNative();  // log only: the corpse lines' killer test reads it, cached here on this thread
            BlackFlame::HitPath::LogHitShaderProbe("kPostLoadGame");
            BlackFlame::HitPath::Detail::ResetIdleResolves();  // log only: resolves spent while the save was read come back
            BlackFlame::HitPath::Detail::NotePostLoad();       // log only: the corpse attach lines say "after a load" (CorpseMark.h)
            BlackFlame::SelfTest::OnGameLoaded(loaded);
            BlackFlame::DevAutomation::OnPostLoadGame();
            FlushLog();
            break;
        }
        case SKSE::MessagingInterface::kNewGame:
            BlackFlame::MenuWatch::Close("kNewGame");
            BlackFlame::Watchdog::NoteLoadEnded();
            TearDown("a new game started");
            if (!g_runtimeReady.load()) {
                logger::warn("BlackFlame: runtime initialization is not ready; skipping new-game runtime work");
                FlushLog();
                break;
            }
            BlackFlame::RefreshCopies("new game");
            BlackFlame::NotifyFreshnessOnce();
            BlackFlame::NotifyFileIndexOnce();
            BlackFlame::SelfTest::OnNewGame();
            BlackFlame::RetryNativeWrappers();
            BlackFlame::RequestRecomputeBuff("new game");
            BlackFlame::LanternIed::OnGameReady("the new game", true);
            FlushLog();
            break;
        case SKSE::MessagingInterface::kSaveGame:
            BlackFlame::Tester::Write("save");  // G5
            BlackFlame::DevAutomation::OnSaveGame();
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
    // GetImpactData redirects (14 each), the SetCastingArt redirect (14), the four v2 prologue detours (Launch, explosion
    // creation, hazard creation, the spawn-hazard window: a 14-byte jump plus a gateway of up to 16 + 14 bytes each,
    // CopySwap.h), the main-thread queue's call (14, MainThreadQueue.h), the hit path observation's three call
    // redirects and the corpse grey hooks A and B (1.6.1170 / 1.7.104 only, 14 each, HitPathObserve.h, CorpseGrey.h): 344 bytes at
    // most; the rest is headroom
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
    if (BlackFlame::GetConfig().singleLaunch) {
        BlackFlame::SlotLedger::RegisterCoSave();  // BFSL: each save keeps the slot pairs it was made with
    }
    // Native wrappers bound when SKSE registers Papyrus functions (after the game's own natives)
    if (auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(BlackFlame::InstallNativeWrappers)) {
        logger::warn("BlackFlame: could not register with the SKSE Papyrus interface - script-placed fire is not tagged");
    }
    logger::info("BlackFlame: loaded");
    return true;
}
