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
#include <unordered_set>

namespace TomeReach
{
    // Game thread, at kDataLoaded: takes the load order and the tomes, starts the pass
    void StartAfterDataLoaded();

    // The spells whose every tome nothing hands out. False while the pass runs,
    // and when it failed (a plugin it could not read): then no spell is marked.
    bool UnreachableSpells(std::unordered_set<RE::FormID>& out);

    // Any thread but the game thread: waits until the pass is done (or failed),
    // at most `limit`. Returns at once when no pass was started. True when done.
    bool WaitUntilDone(std::chrono::milliseconds limit);
}
