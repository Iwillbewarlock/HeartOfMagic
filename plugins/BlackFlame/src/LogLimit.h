#pragma once

#include "Common.h"
#include "Perf.h"
#include "TrackedMutex.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// LOG LIMIT - at most one info line per kind and spell per second
// =============================================================================
// The per-recolour and per-swap lines would flood the log on Flames or a staff of
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
            const Perf::Scope perf(Perf::kLogLimit);  // perf audit 2
            static TrackedMutex mutex;
            static std::unordered_map<std::string, Entry> entries;
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard lock(mutex);
            // Keys are per target and spell: past a cap, entries quiet for a while are dropped (at most
            // one scan a second), and if that is not enough the map starts over (a dropped key just
            // logs its next line), so it cannot grow for the whole session
            static std::chrono::steady_clock::time_point lastPrune{};
            if (entries.size() >= kMaxEntries && now - lastPrune >= kInterval) {
                lastPrune = now;
                std::erase_if(entries, [&](const auto& a_entry) { return now - a_entry.second.last > kStale; });
                if (entries.size() >= kMaxEntries) {
                    entries.clear();
                }
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

        /// v2i: true the first time this kind and form is asked in the session (then false); past kMaxFirstTime keys it
        /// stays false (a session with that many copies logs the first ones only). For one-line "first launch / 3D / art"
        /// notes, which the per-second limit above would also let through again later.
        [[nodiscard]] static bool FirstTime(std::string_view a_kind, RE::FormID a_form) { return Once(a_kind, a_form, true); }

        /// Whether FirstTime would still say true (marks nothing): for a hook that leaves the line to a later task
        [[nodiscard]] static bool NotYet(std::string_view a_kind, RE::FormID a_form) { return Once(a_kind, a_form, false); }

        static constexpr std::size_t kMaxFirstTime = 256;

        static constexpr int kTracePerKey = 6;           // PerKey: lines per key
        static constexpr std::size_t kTraceKeys = 32;    // keys a kind tracks; a key past them gets no line
        static constexpr int kTraceLines = 120;          // lines a kind writes in all

        /// Trace caps per key (a concentration cast cannot use up a kind's lines before another source shows): true for the
        /// first kTracePerKey asks of each key of a kind, while the kind tracks fewer than kTraceKeys keys and has written
        /// fewer than kTraceLines lines; a_line is the kind's line number and a_keyLine the key's (1-based). Any thread
        [[nodiscard]] static bool PerKey(std::string_view a_kind, std::uint64_t a_key, int& a_line, int& a_keyLine)
        {
            const Perf::Scope perf(Perf::kLogLimit);
            static TrackedMutex mutex;
            static auto* kinds = std::make_unique<std::unordered_map<std::string, TraceKind>>().release();  // hooks may run at exit
            std::lock_guard lock(mutex);
            auto& kind = (*kinds)[std::string(a_kind)];
            if (kind.lines >= kTraceLines) {
                return false;
            }
            auto it = kind.keys.find(a_key);
            if (it == kind.keys.end()) {
                if (kind.keys.size() >= kTraceKeys) {
                    return false;
                }
                it = kind.keys.emplace(a_key, 0).first;
            }
            if (it->second >= kTracePerKey) {
                return false;
            }
            a_keyLine = ++it->second;
            a_line = ++kind.lines;
            return true;
        }

    private:
        [[nodiscard]] static bool Once(std::string_view a_kind, RE::FormID a_form, bool a_mark)
        {
            const Perf::Scope perf(Perf::kLogLimit);  // perf audit 2
            static TrackedMutex mutex;
            static std::unordered_set<std::string> seen;
            std::lock_guard lock(mutex);
            const std::string key = std::string(a_kind) + ':' + std::to_string(a_form);
            if (seen.contains(key) || seen.size() >= kMaxFirstTime) {
                return false;
            }
            if (a_mark) {
                seen.insert(key);
            }
            return true;
        }

        struct TraceKind
        {
            int lines = 0;
            std::unordered_map<std::uint64_t, int> keys;
        };

        struct Entry
        {
            std::chrono::steady_clock::time_point last{};
            int suppressed = 0;
            bool written = false;
        };
    };
}
