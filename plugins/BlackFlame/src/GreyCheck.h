#pragma once

#include "Common.h"

#include <string>
#include <vector>

// =============================================================================
// GREY CHECK - the grey swap's setup checked at kDataLoaded, no game play needed
// =============================================================================
// Each grey ADDN record of the map resolves (by its plugin and local id) and sits in
// TESDataHandler::addonNodes at its grey index; each grey model file opens
// (BSResourceNiBinaryStream, through the archives and loose files); the impact call
// sites and both GetModel slots are hooked. Logged as PASS/FAIL at kDataLoaded and
// repeated by the self-test's T0-grey lines.

namespace BlackFlame
{
    struct GreyCheckLine
    {
        bool pass = false;
        std::string text;
    };

    /// kDataLoaded, after the swaps are installed: runs and logs the checks once
    void RunGreyCheck();

    [[nodiscard]] const std::vector<GreyCheckLine>& GreyCheckResults();
}
