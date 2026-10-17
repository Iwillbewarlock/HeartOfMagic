// =============================================================================
// SingleLaunch - the kDataLoaded order of the slot route (SingleLaunch.h)
// =============================================================================

#include "SingleLaunch.h"

#include "FormCopies.h"
#include "FormCopiesFillModel.h"
#include "SlotAssign.h"

#include <chrono>
#include <memory>

namespace BlackFlame::SingleLaunch
{
    namespace
    {
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
        logger::info("BlackFlame: single launch: kDataLoaded part done in {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }

    const SlotKeys::Inputs& Inputs()
    {
        return TheInputs();
    }
}
