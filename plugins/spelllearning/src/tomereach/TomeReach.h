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

#include <chrono>
#include <functional>
#include <string>
#include <cstdint>
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
    bool WaitUntilDone(std::chrono::milliseconds limit);

    // A pass was started and is not done yet (the answer is coming). Any thread.
    bool IsRunning();

    // The tome books handed to the pass of `generation`, and the ones left out because
    // their plugin is not in the loaded set (KeyOf). False when the last pass is not that
    // generation's. Any thread (the dev self-test).
    bool TomeBooks(std::uint64_t generation, std::unordered_set<RE::FormID>& keyed, std::unordered_set<RE::FormID>& skipped);

    // `task(answered)` on the game thread once the pass running now is done (or at
    // once when none runs); answered: it finished with an answer (UnreachableSpells
    // would give one), false when it failed or the wait ran out. The wait is on a
    // thread of its own. False (logged) when no thread could be made: `task` does not run.
    bool RunAfterPass(std::string name, std::function<void(bool answered)> task);
}
