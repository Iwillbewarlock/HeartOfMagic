#include "TrackedMutex.h"

#include <array>
#include <cstring>
#include <format>
#include <memory>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kMaxTracked = 64;

        // Heap-held and never freed: instances are statics of any lifetime
        std::array<std::atomic<const TrackedMutex*>, kMaxTracked>& Registry()
        {
            static auto* registry = std::make_unique<std::array<std::atomic<const TrackedMutex*>, kMaxTracked>>().release();
            return *registry;
        }

        std::atomic<std::size_t> g_count{ 0 };

        // The file name without its folders (the full path is the build machine's)
        const char* ShortName(const char* a_path)
        {
            const char* slash = std::strrchr(a_path, '\\');
            const char* fwd = std::strrchr(a_path, '/');
            const char* last = slash > fwd ? slash : fwd;
            return last ? last + 1 : a_path;
        }
    }

    TrackedMutex::TrackedMutex(std::source_location a_where) noexcept :
        m_file(ShortName(a_where.file_name())),
        m_line(a_where.line())
    {
        const auto index = g_count.fetch_add(1);
        if (index < kMaxTracked) {
            Registry()[index].store(this);
            m_slot = static_cast<int>(index);
        }
    }

    // D2: a destroyed mutex (a static at exit) leaves the registry first, so the watchdog never reads it
    TrackedMutex::~TrackedMutex()
    {
        if (m_slot >= 0) {
            Registry()[static_cast<std::size_t>(m_slot)].store(nullptr);
        }
    }

    void TrackedMutex::lock()
    {
        m_mutex.lock();
        m_owner.store(REX::W32::GetCurrentThreadId(), std::memory_order_relaxed);
    }

    void TrackedMutex::unlock() noexcept
    {
        m_owner.store(0, std::memory_order_relaxed);
        m_mutex.unlock();
    }

    bool TrackedMutex::try_lock()
    {
        if (!m_mutex.try_lock()) {
            return false;
        }
        m_owner.store(REX::W32::GetCurrentThreadId(), std::memory_order_relaxed);
        return true;
    }

    std::vector<std::string> HeldTrackedMutexes()
    {
        std::vector<std::string> out;
        const auto count = std::min(g_count.load(), kMaxTracked);
        for (std::size_t i = 0; i < count; ++i) {
            const auto* mutex = Registry()[i].load();
            if (mutex && mutex->Owner() != 0) {
                out.push_back(std::format("{}:{} held by thread {}", mutex->File(), mutex->Line(), mutex->Owner()));
            }
        }
        return out;
    }
}
