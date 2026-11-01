#include "Common.h"
#include "librarian/JsonPreread.h"
#include "FileUtils.h"
#include "PathText.h"

#include <future>
#include <map>
#include <mutex>
#include <system_error>

// =============================================================================
// LibrarianJsonPreread - see JsonPreread.h
// =============================================================================

namespace Librarian::Detail::Preread
{
    namespace
    {
        struct Stamp
        {
            std::uintmax_t size = 0;
            std::filesystem::file_time_type time;
            bool operator==(const Stamp&) const = default;
        };

        struct Entry
        {
            Stamp stamp;
            nlohmann::json document;
        };

        using Entries = std::map<std::filesystem::path, Entry>;

        std::mutex g_mutex;
        Entries g_entries;   // under g_mutex once g_ready is set
        std::once_flag g_started;
        // Last declared, so destroyed first: an unfinished read is waited for before the entries go
        std::shared_future<void> g_ready;

        bool StampOf(const std::filesystem::path& path, Stamp& stamp)
        {
            std::error_code error;
            stamp.size = std::filesystem::file_size(path, error);
            if (error) return false;
            stamp.time = std::filesystem::last_write_time(path, error);
            return !error;
        }

        // A file that changes while it is read gets a second stamp unlike the first: left out
        void ReadAll(const std::vector<std::filesystem::path>& paths)
        {
            Entries entries;
            for (const auto& path : paths) {
                Stamp before;
                if (!StampOf(path, before)) continue;
                const auto text = FileUtils::ReadAll(path);
                if (!text) continue;
                auto document = nlohmann::json::parse(*text, nullptr, false);
                Stamp after;
                if (document.is_discarded() || !StampOf(path, after) || after != before) continue;
                entries.emplace(path, Entry{ before, std::move(document) });
            }
            std::lock_guard lock(g_mutex);
            g_entries = std::move(entries);
        }
    }

    void Start(std::vector<std::filesystem::path> paths)
    {
        // A worker that cannot start leaves g_ready empty: Take answers false, files are read as before
        try {
            std::call_once(g_started, [&] {
                g_ready = std::async(std::launch::async, [paths = std::move(paths)] {
                    try {
                        ReadAll(paths);
                    } catch (const std::exception& e) {
                        logger::warn("Librarian: reading files ahead failed ({}) - they are read at data load", e.what());
                    }
                }).share();
            });
        } catch (const std::exception& e) {
            logger::warn("Librarian: could not start reading files ahead ({}) - they are read at data load", e.what());
        }
    }

    bool Take(const std::filesystem::path& path, nlohmann::json& document)
    {
        if (!g_ready.valid()) return false;
        g_ready.wait();
        std::lock_guard lock(g_mutex);
        const auto found = g_entries.find(path);
        if (found == g_entries.end()) return false;
        Entry entry = std::move(found->second);
        g_entries.erase(found);
        Stamp now;
        if (!StampOf(path, now) || now != entry.stamp) {
            logger::info("Librarian: '{}' changed since it was read ahead - read again", PathText::Utf8(path));
            return false;
        }
        document = std::move(entry.document);
        return true;
    }
}
