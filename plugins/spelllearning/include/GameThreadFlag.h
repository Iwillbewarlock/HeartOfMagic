#pragma once

// =============================================================================
// GAME THREAD FLAG - which thread may do game thread work (no game types)
// =============================================================================
//
// Not one stamped thread id: SKSE messages and SKSE tasks were seen on different
// threads in one session (kDataLoaded on one, tasks on three others, 2026-10-27
// SpellLearning.log), so the id the last message left behind was no answer.
// Instead the work itself marks its thread: AddTaskToGameThread's wrapper and the
// SKSE message handler open a GameThreadScope.
//
// IsOnGameThread() is true inside such a scope, and on a thread that has run one
// before. The second part is for Papyrus: a native not callable from tasklets runs
// on the thread that runs the tasks, outside any task; posting a task from there and
// waiting for it would wait on itself. A Papyrus worker thread never runs a task, so
// it still posts and waits.
//
// tools/thread-flag-check checks this header; the dev self-test's game-thread line
// checks it in game.

namespace GameThreadFlag
{
    inline thread_local bool t_inScope = false;     // inside a task or a message now
    inline thread_local bool t_ranWork = false;     // this thread has run one (sticky)

    /// Marks the calling thread as doing game thread work for its lifetime
    class Scope
    {
    public:
        Scope() noexcept : m_was(t_inScope)
        {
            t_inScope = true;
            t_ranWork = true;
        }
        ~Scope() { t_inScope = m_was; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        bool m_was;
    };
}

// Whether the caller may do game thread work now. Code that would otherwise submit a
// task and block until it finishes must check this first: the task only runs once this
// thread goes back to its loop, so waiting on it here deadlocks until the wait times out.
[[nodiscard]] inline bool IsOnGameThread() noexcept
{
    return GameThreadFlag::t_inScope || GameThreadFlag::t_ranWork;
}
