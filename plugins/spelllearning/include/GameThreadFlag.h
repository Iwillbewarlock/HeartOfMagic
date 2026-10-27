#pragma once

// =============================================================================
// GAME THREAD FLAG - game thread work marked by the work itself (no game types)
// =============================================================================
//
// IsOnGameThread() (ThreadUtils.h) is true on the engine's main thread
// (RE::Main::threadID) and inside a Scope. The scope is opened where the engine runs
// our code as game thread work off that thread's id: AddTaskToGameThread's wrapper,
// the SKSE message handler and every Papyrus native registered not callable from
// tasklets (RegisterGameThreadNative). One stamped thread id was no answer: in one
// session SKSE messages, tasks and the same event sink each came on several threads
// (2026-10-27 SpellLearning.log), and a thread that once ran a task later ran other
// engine work, so "ran a task once" does not count either.
//
// tools/thread-flag-check checks this header; the dev self-test's game-thread line
// logs which threads the engine used in game.

namespace GameThreadFlag
{
    inline thread_local bool t_inScope = false;   // inside a task, a message or a native now

    /// Marks the calling thread as doing game thread work for the scope's lifetime
    class Scope
    {
    public:
        Scope() noexcept : m_was(t_inScope) { t_inScope = true; }
        ~Scope() { t_inScope = m_was; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        bool m_was;
    };

    [[nodiscard]] inline bool InScope() noexcept
    {
        return t_inScope;
    }
}
