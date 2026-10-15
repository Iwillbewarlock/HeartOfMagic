#pragma once

#include "Common.h"

#include <string>

// =============================================================================
// SELF-TEST LOG - "[SELFTEST] PASS|FAIL|INFO <id> <detail>" lines
// =============================================================================
// Each line goes to HeartOfMagic_BlackFlame.log and to
// HeartOfMagic_BlackFlame_selftest.txt in the SKSE log folder. Game thread only.

namespace BlackFlame::SelfTestLog
{
    inline constexpr const char* kFileName = "HeartOfMagic_BlackFlame_selftest.txt";

    /// Starts a fresh results file (a_append: adds to the existing one, v2 E3 after its reload) and zeroes the counters.
    void Open(bool a_append = false);

    void Pass(const char* a_id, const std::string& a_detail);
    void Fail(const char* a_id, const std::string& a_detail);
    void Info(const char* a_id, const std::string& a_detail);
    /// PASS or FAIL by a_ok
    void Check(const char* a_id, bool a_ok, const std::string& a_detail);

    /// "[SELFTEST] DONE pass=N fail=M info=K" (a_label after DONE when given), then closes the file.
    void Done(const char* a_label = nullptr);
}
