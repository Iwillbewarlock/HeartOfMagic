// =============================================================================
// SingleLaunch - the kDataLoaded order of the slot route, the HUD line and the dev auto-load's readiness (SingleLaunch.h)
// =============================================================================

#include "SingleLaunch.h"

#include "BuildProgress.h"
#include "BuilderLauncher.h"
#include "Config.h"
#include "CopyGroups.h"
#include "CopyRequest.h"
#include "CopyRequestV5.h"
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
        constexpr auto kHudInterval = std::chrono::seconds(10);  // "N of M" at most this often (the "all ready" line always)

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
        std::atomic<bool> g_allReadyShown{ false };
        TrackedMutex g_hudMutex;
        std::optional<Clock::time_point> g_lastHud;
        std::size_t g_lastShown = SIZE_MAX;

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

        void OnMainMenu()
        {
            if (!TheInputs().builderExeFound) {
                SlotNotice::Show("Black Flame: the grey copy builder was not found; only copies built before are shown");
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
        inputs = SlotKeys::Read(a_closure);
        if (!SlotAssign::AssignAndFill(a_closure, inputs)) {
            for (auto& nif : inputs.nifs) {
                std::vector<std::byte>().swap(nif.bytes);  // no slots, no dump: not kept for the session
            }
            return;
        }
        g_routeActive.store(true);
        (void)FormCopies::ApplyLastPublished(inputs);
        const auto request = CopyRequestV5::Write(a_closure, inputs);  // the dump holds the bytes the keys hashed
        TheRequest() = request;
        for (auto& nif : inputs.nifs) {  // before any thread starts (review agy 14_00 #1); nothing after the dump reads them
            std::vector<std::byte>().swap(nif.bytes);  // the dump has them and the keys are made: the NIF bytes are not kept
        }
        BuildProgress::ReadAtStart();  // to its end before the builder starts (it compacts the file at its own start)
        CopyGroups::Start();  // W1: the groups ready from the ledger open now; the rest on the main thread as verified
        LaunchBuilder(inputs, request.written);
        BuildProgress::StartWorker();  // tails the progress and copies finished files into Data while the window is open
        MenuWatch::OnMainMenuOpen(&OnMainMenu);
        logger::info("BlackFlame: single launch: kDataLoaded part done in {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    }

    const SlotKeys::Inputs& Inputs()
    {
        return TheInputs();
    }

    void NotifyProgress()
    {
        if (!g_routeActive.load() || !MenuWatch::MainMenuOpened() || !MenuWatch::PublishOpen() || g_allReadyShown.load()) {
            return;
        }
        const auto progress = CopyGroups::GetProgress();
        if (progress.slots == 0) {
            return;
        }
        if (progress.open >= progress.slots) {
            if (!g_allReadyShown.exchange(true)) {
                SlotNotice::Show("Black Flame: all grey copies are ready");
            }
            return;
        }
        std::lock_guard lock(g_hudMutex);
        const auto now = Clock::now();
        if (progress.open == g_lastShown || (g_lastHud && now - *g_lastHud < kHudInterval)) {
            return;
        }
        g_lastHud = now;
        g_lastShown = progress.open;
        SlotNotice::Show(std::format("Black Flame: {} of {} grey copies ready", progress.open, progress.slots));
    }

    bool ReadyForLoad()
    {
        if (!g_routeActive.load()) {
            return true;
        }
        // review Gemini / agy 14_00: no "caught up and nothing verifying" shortcut - between the worker's catch-up and the
        // main thread's re-plan nothing is verifying yet; settled waits for that re-plan (kStaging and keyless members leave)
        const auto progress = CopyGroups::GetProgress();
        return progress.settled || progress.open >= progress.slots;
    }

    CopyRequestV5::Result Request()
    {
        return TheRequest();
    }
}
