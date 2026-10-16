#pragma once

#include "Common.h"

#include <string>

// =============================================================================
// TESTER FILE - HeartOfMagic_BlackFlame_tester.txt next to the log
// =============================================================================
// One short file a tester sends back with the log: the runtime, every hook (installed or skipped, its address
// as an offset from the image base, the bytes it found, why), the copies (map, fill counts, what was left out,
// the request) and the self-test summary. Rewritten whole at kDataLoaded and again when the self-test ends.

namespace BlackFlame::Tester
{
    /// kDataLoaded, after every hook is installed
    void WriteStartup();

    /// F6: one line of this session's swap counters (Launch, explosion creation, hazard creation and its spawn window, the
    /// casting art site, PlaceAtMe) and the native wrappers' state; game thread (it reads the VM)
    [[nodiscard]] std::string CounterSummary();

    /// The self-test's DONE line (SelfTestLog::Done): the file is written again with it
    void NoteSelfTest(const std::string& a_doneLine);
}
