#pragma once

// =============================================================================
// MAIN MENU WATCH - the plugin's one MainMenu opening sink (at start and when the
// player quits a game to it). Each part that wants to hear of it listens; the
// sink posts their game thread tasks in a fixed order: the panel first (a held
// build at game start goes on, UIManager::NotifyMainMenuShown), then the scan at
// game start (AutoScan::OnMainMenuOpened) - the order the two sinks it replaces
// were registered in (OnDataLoaded).
// =============================================================================

namespace MainMenuWatch
{
    enum class Listener { kPanel, kAutoScan };

    /// Listen from now on (kDataLoaded); the sink is registered at the first call.
    /// False when there is no RE::UI to watch.
    bool Listen(Listener listener);
}
