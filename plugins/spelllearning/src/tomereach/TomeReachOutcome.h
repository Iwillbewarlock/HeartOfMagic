#pragma once

// =============================================================================
// TOME REACH - WHAT A WAIT FOR THE PASS CAME TO
// =============================================================================
//
// RunAfterPass waits for the pass running now, then runs its task on the game
// thread. Between the two the tome pairs can change (an Import, a load): a newer
// generation's pass then runs, and the answer the wait was for is gone. That is
// not a failure - the caller waits again for the new pass - and not a timeout
// either: a wait that ran out while its own pass still runs is a failure, or a
// caller that waits again on it would wait for good. No game types, so
// tools/tome-reach-test --self-test checks it.

#include <cstdint>

namespace TomeReach
{
    enum class PassOutcome
    {
        kAnswered,    // the pass finished with an answer (UnreachableSpells gives it)
        kFailed,      // it failed, the wait ran out, or no pass was started
        kSuperseded,  // a newer generation's pass took over and still runs: wait for that one
    };

    // `waitDone`: the wait saw the pass finish; `waitedGeneration`: the generation
    // when the wait ended; the rest as the task finds them on the game thread.
    [[nodiscard]] constexpr PassOutcome OutcomeOf(bool waitDone, std::uint64_t waitedGeneration,
                                                  std::uint64_t currentGeneration, bool currentReady,
                                                  bool currentRunning) noexcept
    {
        if (currentGeneration != waitedGeneration) {
            if (currentRunning) return PassOutcome::kSuperseded;
            // The newer pass is done already: its answer, or its failure, is the one
            return currentReady ? PassOutcome::kAnswered : PassOutcome::kFailed;
        }
        return waitDone && currentReady ? PassOutcome::kAnswered : PassOutcome::kFailed;
    }

    [[nodiscard]] constexpr const char* Describe(PassOutcome outcome) noexcept
    {
        switch (outcome) {
            case PassOutcome::kAnswered: return "answered";
            case PassOutcome::kFailed: return "failed or did not finish";
            case PassOutcome::kSuperseded: return "superseded by a newer pass";
        }
        return "unknown";
    }
}
