#pragma once

// progression-check's shared parts: the case counter (progression-check.cpp, with the
// runner and the co-save cases) and the cases in progression-check-math.cpp.

#include <string>

namespace ProgressionCheck
{
    inline constexpr float kEpsilon = 1e-4f;

    extern int g_failures;
    extern int g_cases;

    /// One case: PASS or FAIL with its name (and what went wrong) on stdout
    void Check(bool ok, const char* name, const std::string& detail = {});
    /// Equal within kEpsilon
    bool Near(float a, float b);

    // progression-check-math.cpp
    /// ProgressionMath.h: caps, SetSpellXP, study, no-grant, mastery, early grant, passive ticks, AddXP;
    /// ConfigRenames.h's passive setting rename
    void MathCases();
    /// RequiredXPTable.h: required XP by kind, what a revert keeps
    void RequiredXPCases();
}
