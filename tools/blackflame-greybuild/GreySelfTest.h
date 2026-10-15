#pragma once

// =============================================================================
// GreySelfTest - shared by the --self-test files
// =============================================================================

#include "GreyBuild.h"

#include <iostream>
#include <string>

namespace GreyBuild::SelfTest
{
    struct Checker
    {
        int pass = 0;
        int fail = 0;

        void Check(bool a_ok, const std::string& a_what)
        {
            std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
            (a_ok ? pass : fail) += 1;
        }
    };

    /// Request planning (index reuse, collisions, unknown values) and the generated ESL,
    /// on a_sourceNif (AddOnNodes 9 and 24), in a_dir.
    void CheckPlanAndEsl(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);

    /// The v2 copies (--forms): ESL layout, stable ids, refusals, and a whole run with a_sourceNif as a model.
    void CheckForms(Checker& a_check, const std::filesystem::path& a_dir, const std::filesystem::path& a_sourceNif);
}
