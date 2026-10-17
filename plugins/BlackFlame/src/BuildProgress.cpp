// =============================================================================
// BuildProgress - the progress.jsonl tail reader and the staging-to-Data copy worker (CopyContractProgress.h)
// =============================================================================
// Step 5 of P2: the object table CopyGroups reads; the reader and the worker come with the builder launch (step 6).

#include "BuildProgress.h"

#include "TrackedMutex.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace BlackFlame::BuildProgress
{
    namespace
    {
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";

        TrackedMutex g_mutex;
        std::unordered_map<std::uint64_t, Object> g_objects;  // by key, under g_mutex
        Stats g_stats;
        std::atomic<std::uint64_t> g_generation{ 0 };
    }

    void ReadAtStart()
    {
        logger::info("BlackFlame: slots: build progress: no reader yet (P2 step 6)");
    }

    void StartWorker()
    {
    }

    std::optional<Object> Find(std::uint64_t a_key)
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_objects.find(a_key);
        return it != g_objects.end() ? std::optional(it->second) : std::nullopt;
    }

    std::uint64_t Generation() noexcept
    {
        return g_generation.load();
    }

    std::filesystem::path StagingFolder()
    {
        const auto log = SKSE::log::log_directory();
        return log ? *log / kFolderName / CC::kStagingFolder : std::filesystem::path();
    }

    Stats GetStats()
    {
        std::lock_guard lock(g_mutex);
        return g_stats;
    }
}
