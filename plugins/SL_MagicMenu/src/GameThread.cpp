#include "Common.h"

#include "GameThread.h"

#include <atomic>

namespace GameThread
{
    namespace
    {
        std::atomic<std::uint32_t> g_gameThread{ 0 };
    }

    void Set()
    {
        g_gameThread = REX::W32::GetCurrentThreadId();
    }

    bool On()
    {
        const auto game = g_gameThread.load();
        return game != 0 && game == REX::W32::GetCurrentThreadId();
    }

    std::uint32_t Id()
    {
        return g_gameThread.load();
    }
}
