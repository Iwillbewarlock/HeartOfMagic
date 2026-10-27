// thread-flag-check - offline check of the game thread work scope
// (plugins/spelllearning/include/GameThreadFlag.h, used by ThreadUtils.h's IsOnGameThread).
//
//   thread-flag-check --self-test
//
// A thread outside a scope is not in one; inside a Scope it is, nested scopes restore,
// another thread at the same time is not, and once the scope closes the thread is out
// again (no "ran a task once" rule: such a thread later runs other engine work).
// IsOnGameThread's other half, RE::Main::threadID, needs the game: the dev self-test's
// game-thread line.
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
        std::thread([&answer] { answer = GameThreadFlag::InScope(); }).join();
        return answer;
    }

    int SelfTest()
    {
        std::thread([] {
            Check(!GameThreadFlag::InScope(), "a thread outside any scope: not in one");
            {
                const GameThreadFlag::Scope outer;
                Check(GameThreadFlag::InScope(), "inside a scope (a task, a message, a native): in");
                Check(!AskOnNewThread(), "another thread while a scope is open: not in");
                {
                    const GameThreadFlag::Scope inner;
                    Check(GameThreadFlag::InScope(), "a nested scope: in");
                }
                Check(GameThreadFlag::InScope(), "the nested scope closed: the outer one still in");
            }
            Check(!GameThreadFlag::InScope(), "every scope closed: out again (a task once is no rule)");
        }).join();
        Check(!GameThreadFlag::InScope(), "the check's main thread never opened a scope: not in");
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
