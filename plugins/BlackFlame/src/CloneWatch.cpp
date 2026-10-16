#include "CloneWatch.h"
#include "TrackedMutex.h"

#include "SehGuard.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>

namespace BlackFlame
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr auto kWatchTime = std::chrono::seconds(10);
        constexpr int kMaxLines = 12;  // state lines per watch (changes only); the 3D line and the end line come on top

        // BGSArtObjectCloneTask offsets (disassembly, see CloneWatch.h)
        constexpr std::size_t kTaskState = 0x0C;     // BSTask state, 5 = done
        constexpr std::size_t kTaskPending = 0x50;   // BSQueuedResourceCollectionBase pending count
        constexpr std::size_t kTaskQueue = 0x70;     // queue count (queue at +0x58)
        constexpr std::size_t kTaskArt = 0x78;       // the art being cloned
        constexpr std::size_t kTaskResult = 0x80;    // Clone3D's result (NiPointer)
        constexpr std::uint32_t kTaskDone = 5;

        struct TaskRead
        {
            bool task = false;  // the effect had a clone task
            bool read = false;  // its fields were read (no fault)
            std::uint32_t state = 0;
            std::uint32_t pending = 0;
            std::uint32_t queue = 0;
            std::uintptr_t art = 0;
            std::uintptr_t result = 0;

            [[nodiscard]] bool operator==(const TaskRead&) const = default;
        };

        struct Watch
        {
            const RE::BGSArtObject* art = nullptr;
            Clock::time_point armed{};
            int lines = 0;
            std::optional<TaskRead> last;
            bool effectSeen = false;
        };

        TrackedMutex g_mutex;
        std::optional<Watch> g_watch;  // guarded by g_mutex
        std::atomic<bool> g_active{ false };

        template <class T>
        T ReadAt(const void* a_base, std::size_t a_offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::uint8_t*>(a_base) + a_offset, sizeof(T));
            return value;
        }

        long long Ms(Clock::duration a_duration)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(a_duration).count();
        }

        std::string Describe(const TaskRead& a_read)
        {
            if (!a_read.task) {
                return "no clone task";
            }
            if (!a_read.read) {
                return "clone task fields NOT readable (fault)";
            }
            return std::format("clone task state {}{}, pending {}, queue {}, art 0x{:X}, clone result {}", a_read.state,
                a_read.state == kTaskDone ? " (done)" : "", a_read.pending, a_read.queue, a_read.art,
                a_read.result ? std::format("0x{:X}", a_read.result) : std::string("null"));
        }

        struct Seen
        {
            bool found = false;
            bool has3D = false;
            TaskRead task;
        };

        // Inside the engine's ProcessLists lock: comparisons and plain reads only. The task's fields are read under
        // GuardedKeep inside the callback, so a fault is caught there and never unwinds through the lock's frame.
        Seen Look(const RE::BGSArtObject* a_art)
        {
            Seen seen;
            auto* lists = RE::ProcessLists::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!lists || !player) {
                return seen;
            }
            const auto target = player->GetHandle();
            lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
                if (!a_effect || a_effect->finished || a_effect->artObject != a_art || a_effect->target != target) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                seen.found = true;
                seen.has3D = a_effect->artObject3D != nullptr;
                const void* task = a_effect->cloneTask.get();
                seen.task.task = task != nullptr;
                if (task) {
                    seen.task.read = GuardedKeep("clone watch task read", [&]() {
                        seen.task.state = ReadAt<std::uint32_t>(task, kTaskState);
                        seen.task.pending = ReadAt<std::uint32_t>(task, kTaskPending);
                        seen.task.queue = ReadAt<std::uint32_t>(task, kTaskQueue);
                        seen.task.art = ReadAt<std::uintptr_t>(task, kTaskArt);
                        seen.task.result = ReadAt<std::uintptr_t>(task, kTaskResult);
                    });
                }
                return RE::BSContainer::ForEachResult::kStop;
            });
            return seen;
        }

        void Stop()
        {
            g_watch.reset();
            g_active.store(false);
        }
    }

    void ArmCloneWatch(const RE::BGSArtObject* a_copy)
    {
        if (!a_copy) {
            return;
        }
        std::lock_guard lock(g_mutex);
        if (g_watch && g_watch->art == a_copy && Clock::now() - g_watch->armed < kWatchTime) {
            return;  // the engine asks the art up to three times per effect (Start, Init's gate): keep the first time
        }
        g_watch = Watch{ a_copy, Clock::now() };
        g_active.store(true);
    }

    void TickCloneWatch()
    {
        if (!g_active.load()) {
            return;
        }
        // The art is copied out and the walk runs without g_mutex: the hook that arms the watch may run while the engine
        // holds the ProcessLists lock, so holding g_mutex across the walk could deadlock
        const RE::BGSArtObject* art = nullptr;
        {
            std::lock_guard lock(g_mutex);
            if (!g_watch) {
                return;
            }
            art = g_watch->art;
        }
        Seen seen;
        const bool walked = Guarded("clone watch", [&]() { seen = Look(art); });
        std::lock_guard lock(g_mutex);
        if (!g_watch || g_watch->art != art) {
            return;  // reset or re-armed meanwhile
        }
        if (!walked) {
            Stop();
            return;
        }
        auto& watch = *g_watch;
        const auto artId = art->GetFormID();
        const auto elapsed = Clock::now() - watch.armed;
        if (seen.found && seen.has3D) {
            logger::info("BlackFlame: clone watch: copy hit art {:08X} on the player: artObject3D set {} ms after the hook "
                         "handed the copy out; {}", artId, Ms(elapsed), Describe(seen.task));
            Stop();
            return;
        }
        if (elapsed >= kWatchTime) {
            logger::info("BlackFlame: clone watch: copy hit art {:08X}: NO 3D {} ms after the hook handed it out ({}); last: {}",
                artId, Ms(elapsed), watch.effectSeen ? "its effect was seen on the player" : "no live effect with it on the player",
                watch.last ? Describe(*watch.last) : std::string("nothing read"));
            Stop();
            return;
        }
        if (!seen.found) {
            if (watch.effectSeen && watch.lines < kMaxLines) {
                ++watch.lines;
                logger::info("BlackFlame: clone watch: copy hit art {:08X}: its effect is gone from the player after {} ms, no 3D",
                    artId, Ms(elapsed));
                Stop();
            }
            return;
        }
        watch.effectSeen = true;
        if (watch.last && *watch.last == seen.task) {
            return;
        }
        watch.last = seen.task;
        if (watch.lines < kMaxLines) {
            ++watch.lines;
            logger::info("BlackFlame: clone watch: copy hit art {:08X} on the player, {} ms, no 3D yet: {}", artId, Ms(elapsed),
                Describe(seen.task));
        }
    }

    bool CloneWatchActive() noexcept
    {
        return g_active.load();
    }

    void ResetCloneWatch()
    {
        std::lock_guard lock(g_mutex);
        Stop();
    }
}
