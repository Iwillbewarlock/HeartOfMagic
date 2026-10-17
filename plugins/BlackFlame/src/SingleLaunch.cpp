// =============================================================================
// SingleLaunch - the kDataLoaded order of the slot route (SingleLaunch.h)
// =============================================================================

#include "SingleLaunch.h"

#include "BuildProgress.h"
#include "BuilderLauncher.h"
#include "Config.h"
#include "CopyGroups.h"
#include "CopyRequest.h"
#include "FormCopies.h"
#include "FormCopiesFillModel.h"
#include "SlotAssign.h"

#include <chrono>
#include <memory>

namespace BlackFlame::SingleLaunch
{
    namespace
    {
        // The builder as a child, when the config wants it and a request is there for it (CopyRequest.h)
        void LaunchBuilder(const SlotKeys::Inputs& a_inputs)
        {
            const auto& config = GetConfig();
            const auto folder = RequestFolder();
            std::error_code ec;
            if (!config.launchBuilder) {
                logger::info("BlackFlame: slots: builder not started (launchBuilder false)");
            } else if (!a_inputs.prefix) {
                logger::info("BlackFlame: slots: builder not started: {}", a_inputs.prefixWhy);
            } else if (!folder || !std::filesystem::exists(*folder / "request.json", ec)) {
                logger::warn("BlackFlame: slots: builder not started: no request.json in the request folder");
            } else {
                (void)BuilderLauncher::Start(a_inputs.builderExe, *folder, BuildProgress::StagingFolder(), config.builderJobMemoryMiB);
            }
        }

        SlotKeys::Inputs& TheInputs()
        {
            static auto* inputs = std::make_unique<SlotKeys::Inputs>().release();
            return *inputs;
        }
    }

    void OnDataLoaded(const FireClosure& a_closure)
    {
        const auto start = std::chrono::steady_clock::now();
        // The copy model probe does not run on this route (CopyGroups verifies before it opens): its result is "nothing taken
        // out", so the self-test's wait for it ends at once
        DisableCopiesForSession({}, {});
        auto& inputs = TheInputs();
        inputs = SlotKeys::Read(a_closure);
        if (!SlotAssign::AssignAndFill(a_closure, inputs)) {
            return;
        }
        (void)FormCopies::ApplyLastPublished(inputs);
        BuildProgress::ReadAtStart();  // to its end before the builder starts (it compacts the file at its own start)
        CopyGroups::Start();  // W1: the groups ready from the ledger open now; the rest on the main thread as verified
        LaunchBuilder(inputs);
        BuildProgress::StartWorker();  // tails the progress and copies finished files into Data while the window is open
        logger::info("BlackFlame: single launch: kDataLoaded part done in {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }

    const SlotKeys::Inputs& Inputs()
    {
        return TheInputs();
    }
}
