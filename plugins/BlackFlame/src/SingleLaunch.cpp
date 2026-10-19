// =============================================================================
// SingleLaunch - the kDataLoaded order of the slot route, the progress log line and the dev auto-load's readiness (SingleLaunch.h)
// =============================================================================

#include "SingleLaunch.h"

#include "BuildProgress.h"
#include "BuilderLauncher.h"
#include "Config.h"
#include "CopyGroups.h"
#include "CopyRequest.h"
#include "CopyRequestV5.h"
#include "DataFiles.h"
#include "FormCopies.h"
#include "FormCopiesFillModel.h"
#include "MenuWatch.h"
#include "SlotAssign.h"
#include "SlotNotice.h"
#include "TrackedMutex.h"

#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <optional>

namespace BlackFlame::SingleLaunch
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto kProgressLogInterval = std::chrono::seconds(10);  // the "N of M" log line at most this often (NotifyProgress
                                                                         // runs at every publish step); "all ready" once

        SlotKeys::Inputs& TheInputs()
        {
            static auto* inputs = std::make_unique<SlotKeys::Inputs>().release();
            return *inputs;
        }

        CopyRequestV5::Result& TheRequest()  // kDataLoaded writes it; the self-test reads it later on the main thread
        {
            static auto* request = std::make_unique<CopyRequestV5::Result>().release();
            return *request;
        }

        std::atomic<bool> g_routeActive{ false };
        std::atomic<bool> g_otherGame{ false };  // another game process holds the request folder (review round 1 D2)
        std::atomic<bool> g_allReadyLogged{ false };
        TrackedMutex g_progressMutex;
        std::optional<Clock::time_point> g_lastProgressLog;
        std::size_t g_lastLogged = SIZE_MAX;

        // The builder as a child, when the config wants it and request v5 was written for it
        void LaunchBuilder(const SlotKeys::Inputs& a_inputs, bool a_requestWritten)
        {
            const auto& config = GetConfig();
            const auto folder = RequestFolder();
            if (!config.launchBuilder) {
                logger::info("BlackFlame: slots: builder not started (launchBuilder false)");
            } else if (!a_inputs.prefix) {
                logger::info("BlackFlame: slots: builder not started: {}", a_inputs.prefixWhy);
            } else if (!folder || !a_requestWritten) {
                logger::warn("BlackFlame: slots: builder not started: request v5 was not written");
            } else {
                (void)BuilderLauncher::Start(a_inputs.builderExe, *folder, BuildProgress::StagingFolder(), config.builderJobMemoryMiB);
            }
        }

        // The slot route did not start: one line at the main menu, in plain words (the log has the details)
        void ShowRouteOff(std::string a_why)
        {
            a_why += BuilderControl::BuilderRouteOffClause(TheInputs().builderKind);  // not found / could not be read / another version
            const auto line = "Black Flame: grey copies are off this session (" + a_why + ")";
            logger::warn("BlackFlame: single launch: the slot route did not start: \"{}\" at the main menu", line);
            MenuWatch::OnMainMenuOpen([line]() { SlotNotice::Show(line); });
        }

        void OnMainMenu()
        {
            const auto& inputs = TheInputs();
            if (const auto notice = BuilderControl::BuilderNotice(inputs.builderKind); !notice.empty()) {
                if (!inputs.builderIdentityWhy.empty()) {
                    logger::warn("BlackFlame: single launch: the grey copy builder {}: no key this session", inputs.builderIdentityWhy);
                }
                SlotNotice::Show(notice);
            }
            NotifyProgress();
        }
    }

    void OnDataLoaded(const FireClosure& a_closure)
    {
        const auto start = Clock::now();
        // The copy model probe does not run on this route (CopyGroups verifies before it opens): its result is "nothing taken
        // out", so the self-test's wait for it ends at once
        DisableCopiesForSession({}, {});
        auto& inputs = TheInputs();
        std::string offWhy;
        bool assigned = false;
        // review: a throw in the route's start still tells the player (SlotLedger::Load runs in Main.cpp's guard; a throw
        // there leaves the ledger unavailable, which AssignAndFill reports as "no ledger")
        try {
            inputs = SlotKeys::Read(a_closure);
            assigned = SlotAssign::AssignAndFill(a_closure, inputs, offWhy);
        } catch (const std::exception& e) {
            logger::error("BlackFlame: single launch: the slot route's start threw: {}", e.what());
            offWhy = "the slots could not be set up";
        } catch (...) {
            logger::error("BlackFlame: single launch: the slot route's start threw an exception of an unknown type");
            offWhy = "the slots could not be set up";
        }
        if (!assigned) {
            for (auto& nif : inputs.nifs) {
                std::vector<std::byte>().swap(nif.bytes);  // no slots, no dump: not kept for the session
            }
            ShowRouteOff(offWhy.empty() ? "the slots could not be set up" : offWhy);
            return;
        }
        g_routeActive.store(true);
        // review round 1 D2: the request folder's claim before the request, the dump or a deferred copy is touched (also with
        // launchBuilder off: those are written either way). Another game holds it: none of them, no builder, no worker
        // (BuilderLauncher::ClaimFolder logs it once); no request folder: not excluded, as before
        const auto folder = RequestFolder();
        const bool otherGame = folder && !BuilderFolderMutex::MayStart(BuilderLauncher::ClaimFolder(*folder));
        g_otherGame.store(otherGame);
        if (!otherGame) {
            BuildProgress::ApplyDeferred();  // before anything uses a copy path this session (review N3)
        }
        (void)FormCopies::ApplyLastPublished(inputs);
        // the dump holds the bytes the keys hashed
        const auto request = otherGame ? CopyRequestV5::Result{} : CopyRequestV5::Write(a_closure, inputs);
        TheRequest() = request;
        for (auto& nif : inputs.nifs) {  // before any thread starts (review agy 14_00 #1); nothing after the dump reads them
            std::vector<std::byte>().swap(nif.bytes);  // the dump has them and the keys are made: the NIF bytes are not kept
        }
        BuildProgress::ReadAtStart();  // to its end before the builder starts (it compacts the file at its own start)
        if (otherGame) {
            BuildProgress::SkipWorker();  // nothing more is read this session: the start's read is caught up (before W1 plans)
        }
        CopyGroups::Start();  // W1: the groups ready from the ledger open now; the rest on the main thread as verified
        if (!otherGame) {
            LaunchBuilder(inputs, request.written);
            BuildProgress::StartWorker();  // tails the progress and copies finished files into Data while the window is open
        }
        MenuWatch::OnMainMenuOpen(&OnMainMenu);
        const auto hashed = DataFiles::GetStats();  // review: the kDataLoaded hashing cost, by itself
        logger::info("BlackFlame: single launch: kDataLoaded part done in {} ms; {} Data file(s) hashed ({} KiB) in {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count(), hashed.files, hashed.bytes / 1024,
            hashed.micros / 1000);
    }

    const SlotKeys::Inputs& Inputs()
    {
        return TheInputs();
    }

    void NotifyProgress()
    {
        // bf-no-wait-proposal.md section 5 (user's choice 4a): log lines only, nothing on screen - a count invites waiting,
        // and what is not open when the window closes simply opens at the next launch
        if (!g_routeActive.load() || !MenuWatch::MainMenuOpened() || !MenuWatch::PublishOpen() || g_allReadyLogged.load()) {
            return;
        }
        const auto progress = CopyGroups::GetProgress();
        if (progress.slots == 0) {
            return;
        }
        if (progress.open >= progress.slots) {
            if (!g_allReadyLogged.exchange(true)) {
                logger::info("BlackFlame: slots: progress: all {} grey copies are ready (log only)", progress.slots);
            }
            return;
        }
        std::lock_guard lock(g_progressMutex);
        const auto now = Clock::now();
        if (progress.open == g_lastLogged || (g_lastProgressLog && now - *g_lastProgressLog < kProgressLogInterval)) {
            return;
        }
        g_lastProgressLog = now;
        g_lastLogged = progress.open;
        logger::info("BlackFlame: slots: progress: {} of {} grey copies ready (log only)", progress.open, progress.slots);
    }

    bool ReadyForLoad()
    {
        if (!g_routeActive.load()) {
            return true;
        }
        // review Gemini / agy 14_00: no "caught up and nothing verifying" shortcut - between the worker's catch-up and the
        // main thread's re-plan nothing is verifying yet; settled waits for that re-plan (kStaging and keyless members leave)
        // (publication fix, 2026-10-19) every slot open is not enough while an open group still verifies newer objects for
        // the next start: a load now would leave them to be verified again then
        const auto progress = CopyGroups::GetProgress();
        return progress.settled || (progress.open >= progress.slots && progress.upgrading == 0);
    }

    CopyRequestV5::Result Request()
    {
        return TheRequest();
    }

    bool OtherGameHoldsFolder()
    {
        return g_otherGame.load();
    }
}
