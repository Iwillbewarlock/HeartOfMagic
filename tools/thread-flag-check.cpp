// thread-flag-check - offline check of IsOnGameThread()
// (plugins/spelllearning/include/GameThreadFlag.h, used by ThreadUtils.h).
//
//   thread-flag-check --self-test
//
// A thread that never did game thread work says no; inside a Scope yes, nested
// scopes restore; another thread at the same time still says no; a thread that ran
// a Scope says yes afterwards (the thread that runs tasks also runs Papyrus natives
// outside them, and must not wait on a task of its own).
//
// Exit 0 when every case passes, 1 on a failure, 2 on a bad argument.

#include "GameThreadFlag.h"

#include <cstdio>
#include <string_view>
#include <thread>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitFailed = 1;
    constexpr int kExitUsage = 2;

    int g_failures = 0;
    int g_checks = 0;

    void Check(bool ok, const char* what)
    {
        ++g_checks;
        if (!ok) {
            ++g_failures;
            std::printf("FAIL %s\n", what);
        }
    }

    bool AskOnNewThread()
    {
        bool answer = true;
        std::thread([&answer] { answer = IsOnGameThread(); }).join();
        return answer;
    }

    int SelfTest()
    {
        std::thread([] {
            Check(!IsOnGameThread(), "a thread that never did game thread work: no");
            {
                const GameThreadFlag::Scope outer;
                Check(IsOnGameThread(), "inside a scope (a task, a message): yes");
                Check(!AskOnNewThread(), "another thread while a scope is open: no");
                {
                    const GameThreadFlag::Scope inner;
                    Check(GameThreadFlag::t_inScope, "a nested scope: still in");
                }
                Check(GameThreadFlag::t_inScope, "the nested scope closed: the outer one still in");
            }
            Check(!GameThreadFlag::t_inScope, "every scope closed: out of the scope");
            Check(IsOnGameThread(), "a thread that ran game thread work: yes after it (no wait on itself)");
        }).join();
        Check(!IsOnGameThread(), "the main thread of this check never opened a scope: no");
        std::printf("thread-flag-check: %d checks, %d failed\n", g_checks, g_failures);
        return g_failures == 0 ? kExitOk : kExitFailed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--self-test") {
        std::fprintf(stderr, "usage: thread-flag-check --self-test\n");
        return kExitUsage;
    }
    return SelfTest();
}
