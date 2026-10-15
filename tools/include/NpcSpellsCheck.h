#pragma once
#include <cstdio>
namespace NpcSpellsCheck {
    inline int g_failed = 0;
    inline int g_passed = 0;
    inline void Check(bool ok, const char* what) {
        (ok ? g_passed : g_failed)++;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    }
}
int RunRulesSelfTest();
int RunBuildSelfTest();
int RunNpcSelfTest();
int RunEslSelfTest();
int RunDistrSelfTest();
