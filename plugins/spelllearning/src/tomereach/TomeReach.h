#pragma once

// =============================================================================
// TOME REACH - IN GAME
// =============================================================================
//
// After the data loads, a worker thread reads the load order's plugin files and
// distribution configs (TomeReachPass) and finds the spells whose every tome
// nothing hands out: no placed copy, no leveled list, container, inventory,
// recipe, form list, quest item or script property, no distribution config.
// The scan marks them tomeUnreachable and the tree leaves them out.
// See docs/ARCHITECTURE.md, "Tomes nothing hands out".

#include "tomereach/TomeReachOutcome.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

namespace TomeReach
{
    // Game thread, at kDataLoaded: takes the load order and the tomes, starts the pass
    void StartAfterDataLoaded();

    // Game thread: the tome pairs changed (SpellTomes.h, generation `generation`):
    // the pass runs again with this generation's tomes. Until it is done no spell
    // is marked; a pass of an older generation that finishes later is dropped.
    void Restart(std::uint64_t generation);

    // The spells whose every tome nothing hands out. False while the pass runs,
    // and when it failed (a plugin it could not read): then no spell is marked.
    bool UnreachableSpells(std::unordered_set<RE::FormID>& out);

    // Any thread but the game thread: waits until the pass is done (or failed),
    // at most `limit`. Returns at once when no pass was started. True when done.
    // `generation`, when given: the generation as the wait ended, read under the same
    // lock (RunAfterPass tells a newer pass from a timeout by it).
    bool WaitUntilDone(std::chrono::milliseconds limit, std::uint64_t* generation = nullptr);

    // A pass was started and is not done yet (the answer is coming). Any thread.
    bool IsRunning();

    // The tome books handed to the pass of `generation`, and the ones left out because
    // their plugin is not in the loaded set (KeyOf). False when the last pass is not that
    // generation's. Any thread (the dev self-test).
    bool TomeBooks(std::uint64_t generation, std::unordered_set<RE::FormID>& keyed, std::unordered_set<RE::FormID>& skipped);

    // `task(outcome)` on the game thread once the pass running now is done (or at
    // once when none runs). kAnswered: it finished with an answer (UnreachableSpells
    // gives one); kFailed: it failed or the wait ran out; kSuperseded: the tome pairs
    // changed before the task ran and a newer pass runs - wait for that one
    // (TomeReachOutcome.h). The wait is on a thread of its own. False (logged) when no
    // thread could be made: `task` does not run.
    bool RunAfterPass(std::string name, std::function<void(PassOutcome outcome)> task);

    // How the pass ended, for the scan record (ScanRecordRules::WithTomeReach):
    // "ready", "failed:unreadable:<FNV of the sorted unreadable plugin names>",
    // "failed:error" (it threw), "not started"; empty while it runs. A pass that
    // fails the same way again gives the same key, so its scan stays recorded.
    // Any thread.
    std::string OutcomeKey();
}
