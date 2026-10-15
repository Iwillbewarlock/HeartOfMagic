#pragma once

#include "Common.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

// =============================================================================
// LOG LIMIT - at most one info line per kind and spell per second
// =============================================================================
// The per-recolour and per-burn lines would flood the log on Flames or a staff of
// projectiles. Allow() says whether to write this one, and how many were held
// back since the last written line. Self-test lines do not go through it.

namespace BlackFlame
{
    class LogLimit
    {
    public:
        static constexpr auto kInterval = std::chrono::seconds(1);
        static constexpr std::size_t kMaxEntries = 512;
        static constexpr auto kStale = std::chrono::seconds(10);

        /// True when a line of this kind for this spell may be written now; a_suppressed
        /// gets the number held back since the last one written (then reset).
        [[nodiscard]] static bool Allow(std::string_view a_kind, RE::FormID a_spell, int& a_suppressed)
        {
            static std::mutex mutex;
            static std::unordered_map<std::string, Entry> entries;
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard lock(mutex);
            // Keys are per target and spell: entries quiet for a while are dropped past a cap, so the
            // map does not grow for the whole session (a dropped one just logs its next line)
            if (entries.size() > kMaxEntries) {
                std::erase_if(entries, [&](const auto& a_entry) { return now - a_entry.second.last > kStale; });
            }
            auto& entry = entries[std::string(a_kind) + ':' + std::to_string(a_spell)];
            if (entry.written && now - entry.last < kInterval) {
                ++entry.suppressed;
                return false;
            }
            a_suppressed = entry.suppressed;
            entry = { now, 0, true };
            return true;
        }

    private:
        struct Entry
        {
            std::chrono::steady_clock::time_point last{};
            int suppressed = 0;
            bool written = false;
        };
    };
}
