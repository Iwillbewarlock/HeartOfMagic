// =============================================================================
// SELF-TEST v2 STEPS - the copy framework spike (README "Black Flame v2")
// =============================================================================

#include "SelfTestCtx.h"

#include "GameThread.h"
#include "SelfTestLog.h"
#include "ThreadTrace.h"

#include <format>

namespace BlackFlame::SelfTest
{
    // T-threads (S4): which threads ran our task bodies and v2 hooks, and whether bodies on different
    // threads overlapped. Measurement only; no gate depends on it. Meant to observe: no recorded body is a FAIL.
    bool StepThreads(Ctx&, bool)
    {
        const auto entries = ThreadTrace::Snapshot();
        const auto game = g_gameThreadNativeId.load();
        for (const auto& line : ThreadTrace::Report(game)) {
            SelfTestLog::Info("T-threads", line);
        }
        std::size_t onOther = 0;
        for (const auto& e : entries) {
            onOther += e.thread != game ? 1 : 0;
        }
        SelfTestLog::Check("T-threads", !entries.empty(), std::format("{} traced bodies, {} not on the stamped game thread, "
            "{} overlapping pairs on different threads (measurement: the thread gates stay as they are)", entries.size(),
            onOther, ThreadTrace::CountCrossThreadOverlaps(entries)));
        return true;
    }
}
