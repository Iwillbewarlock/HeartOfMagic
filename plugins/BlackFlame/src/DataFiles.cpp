// =============================================================================
// DataFiles - a copy file under the Data folder: its size, and its FNV-1a 64 read once a session (DataFiles.h)
// =============================================================================

#include "DataFiles.h"

#include "FileIndex.h"
#include "NifDump.h"
#include "TrackedMutex.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace BlackFlame::DataFiles
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr std::size_t kReadChunk = 1 << 16;

        struct Cached
        {
            std::uint64_t size = 0;
            fs::file_time_type written{};
            std::uint64_t fnv64 = 0;
        };

        struct Cache
        {
            TrackedMutex mutex;
            std::unordered_map<std::string, Cached> files;
        };
        Cache& TheCache()
        {
            static auto* cache = std::make_unique<Cache>().release();  // leaked: worker threads may use it at exit
            return *cache;
        }

        std::optional<fs::path> FileOf(const std::string& a_dataPath)
        {
            const auto data = FileIndex::DataFolder();
            return data ? std::optional(*data / fs::path(a_dataPath)) : std::nullopt;
        }
    }

    std::optional<std::uint64_t> SizeOf(const std::string& a_dataPath)
    {
        const auto file = FileOf(a_dataPath);
        std::error_code ec;
        const auto size = file ? fs::file_size(*file, ec) : 0;
        return file && !ec ? std::optional<std::uint64_t>(size) : std::nullopt;
    }

    std::optional<Hash> HashOf(const std::string& a_dataPath)
    {
        const auto file = FileOf(a_dataPath);
        if (!file) {
            return std::nullopt;
        }
        std::error_code ec;
        const auto size = fs::file_size(*file, ec);
        const auto written = ec ? fs::file_time_type{} : fs::last_write_time(*file, ec);
        if (ec) {
            return std::nullopt;
        }
        auto& cache = TheCache();
        {
            std::lock_guard lock(cache.mutex);
            if (const auto it = cache.files.find(a_dataPath); it != cache.files.end() && it->second.size == size && it->second.written == written) {
                return Hash{ size, it->second.fnv64 };
            }
        }
        std::ifstream in(*file, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::uint64_t hash = NifDump::kFnvOffset;
        std::uint64_t read = 0;
        std::vector<char> chunk(kReadChunk);
        while (in.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || in.gcount() > 0) {
            const auto count = static_cast<std::size_t>(in.gcount());
            hash = NifDump::Fnv64(chunk.data(), count, hash);
            read += count;
        }
        std::lock_guard lock(cache.mutex);
        cache.files[a_dataPath] = { read, written, hash };
        return Hash{ read, hash };
    }

    bool Matches(const std::string& a_dataPath, std::uint64_t a_size, std::uint64_t a_fnv64)
    {
        const auto size = SizeOf(a_dataPath);
        if (!size || *size != a_size) {
            return false;  // the size first: no byte read for a file that cannot be it
        }
        const auto hash = HashOf(a_dataPath);
        return hash && hash->size == a_size && hash->fnv64 == a_fnv64;
    }

    void Forget(const std::string& a_dataPath)
    {
        auto& cache = TheCache();
        std::lock_guard lock(cache.mutex);
        cache.files.erase(a_dataPath);
    }
}
