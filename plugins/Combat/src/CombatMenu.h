#pragma once

// =============================================================================
// CombatMenu - SL_Combat's page in SKSE Menu Framework
// =============================================================================
// Section "Heart of Magic Combat", item "Counter". The page edits a working
// copy of CombatConfig on the render thread; "Apply and save" hands the copy to
// the game thread (an SKSE task), which makes it live and writes
// Data/SKSE/Plugins/SL_Combat.json. Without SKSE Menu Framework nothing is
// registered and the JSON file is the only way to change settings.
// =============================================================================

namespace SLCombat
{
    class CombatMenu
    {
    public:
        /// Called at kDataLoaded, after the counter is set up.
        static void Register(bool a_counterActive);

    private:
        static void __stdcall Render();
    };
}
