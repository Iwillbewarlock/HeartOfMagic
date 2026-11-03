#pragma once

// magic-menu-hom-block-check's shared helpers (the PASS/FAIL counters and text), for the check's sources:
// magic-menu-hom-block-check.cpp (settings, pick rules, strings, the cards' and the tree window's arithmetic) and
// magic-menu-hom-block-check-choices.cpp (the progress card's next-spell rows, 2026-11-03)

#include <cmath>
#include <cstdio>
#include <string>

namespace HomBlockCheck
{
    inline int g_pass = 0;
    inline int g_fail = 0;

    inline void Expect(bool a_ok, const std::string& a_what)
    {
        std::printf("  %s  %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
        (a_ok ? g_pass : g_fail) += 1;
    }

    inline bool Near(double a_lhs, double a_rhs, double a_tolerance)
    {
        return std::abs(a_lhs - a_rhs) <= a_tolerance;
    }

    inline std::string Text(const char* a_format, auto... a_values)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), a_format, a_values...);
        return buffer;
    }

    // magic-menu-hom-block-check-choices.cpp
    void Choices();
}
