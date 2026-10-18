#pragma once

// =============================================================================
// BUILDER CONTROL TESTS - the throttle's CPU rate and the builder's exit code in words (BuilderControl.h)
// =============================================================================
// The DLL's copies of the builder's exit codes are pinned to the builder's own (GreyBuild.h, GreyBudget.h: both include only
// the standard library). Included by main.cpp.

#include "../../plugins/BlackFlame/src/BuilderControl.h"
#include "../blackflame-greybuild/GreyBudget.h"
#include "../blackflame-greybuild/GreyBuild.h"

#include <cstdint>
#include <string>

namespace BuilderControlTests
{
    namespace BC = BlackFlame::BuilderControl;

    static_assert(BC::kExitOk == static_cast<std::uint32_t>(GreyBuild::kExitOk), "the builder's kExitOk moved");
    static_assert(BC::kExitFail == static_cast<std::uint32_t>(GreyBuild::kExitFail), "the builder's kExitFail moved");
    static_assert(BC::kExitUsage == static_cast<std::uint32_t>(GreyBuild::kExitUsage), "the builder's kExitUsage moved");
    static_assert(BC::kExitParentGone == static_cast<std::uint32_t>(GreyBuild::kExitParentGone), "the builder's kExitParentGone moved");

    template <class Check>
    void Rate(Check&& a_check, std::uint32_t a_cpus, std::uint32_t a_expected, const std::string& a_what)
    {
        const auto rate = BC::CpuRateForOneCpu(a_cpus);
        a_check(rate == a_expected && rate != 0,
            "cpu rate: " + a_what + " (" + std::to_string(a_cpus) + " logical CPUs -> " + std::to_string(rate) + "/10000)");
    }

    template <class Check>
    void Exit(Check&& a_check, std::uint32_t a_code, const std::string& a_expected, const std::string& a_what)
    {
        const auto text = BC::ExitCodeText(a_code);
        a_check(text == a_expected, "exit code: " + a_what + " ('" + text + "')");
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        Rate(a_check, 1, 10000, "one CPU is the whole machine");
        Rate(a_check, 4, 2500, "4");
        Rate(a_check, 16, 625, "16 (this machine)");
        Rate(a_check, 64, 156, "64 (rounded down)");
        Rate(a_check, 3, 3333, "3 (rounded down)");
        Rate(a_check, 0, 10000, "0 read as one CPU, never a rate of 0");
        Rate(a_check, 20000, 1, "more CPUs than 10000: 1, never 0");

        Exit(a_check, 0, "0 (ok: every object has a line)", "0 ok");
        Exit(a_check, 1, "1 (failed: the run could not go on; builder.log says why)", "kExitFail");
        Exit(a_check, 2, "2 (refused: bad arguments or a request that is not version 5)", "kExitUsage");
        Exit(a_check, 3, "3 (stopped: the parent process is gone)", "kExitParentGone");
        Exit(a_check, 0xC0000005u, "0xC0000005 (terminated or crashed)", "an access violation");
        Exit(a_check, 4, "0x00000004 (terminated or crashed)", "an unknown small code");
        Exit(a_check, 0xFFFFFFFFu, "0xFFFFFFFF (terminated or crashed)", "the largest code");
    }
}
