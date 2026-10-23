// =============================================================================
// DataFiles - a copy file under the Data folder: its size, and its FNV-1a 64 read once a session (DataFiles.h)
// =============================================================================

#include "DataFiles.h"

#include "FileIndex.h"
#include "NifDump.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BlackFlame::DataFiles
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr std::size_t kReadChunk = 1 << 16;
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";  // as BuildProgress' and CopyRequest's
        constexpr const char* kPersistStem = "datafiles-hashes";  // datafiles-hashes.<8 hex of the Data folder>.tsv
        constexpr std::string_view kPersistHeader = "HoMBF datafiles v1";
        constexpr std::string_view kPersistTrailer = "end";  // "end\t<entries>\t<fnv64 of every line before it>"
        constexpr char kField = '\t';

        struct Cached
        {
            std::uint64_t size = 0;
            fs::file_time_type written{};
            std::uint64_t fnv64 = 0;
            bool confirmed = false;  // read or checked this session: persisted (an entry only loaded is dropped then)
        };

        struct Cache
        {
            TrackedMutex mutex;
            std::unordered_map<std::string, Cached> files;
            std::uint64_t loaded = 0;   // entries taken from the last session's file
            std::uint64_t reused = 0;   // of them, used this session (no byte read)
        };

        // review: one file per Data folder (installs and MO2 instances sharing the Documents folder keep their own), named
        // as the ledger mirror is: the low 32 bits of the FNV-1a 64 of the lower-cased path
        std::optional<fs::path> PersistFile()
        {
            const auto log = SKSE::log::log_directory();
            const auto data = FileIndex::DataFolder();
            if (!log || !data) {
                return std::nullopt;
            }
            auto text = data->lexically_normal().string();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
            const auto id = static_cast<std::uint32_t>(NifDump::Fnv64(text.data(), text.size(), NifDump::kFnvOffset));
            return *log / kFolderName / std::format("{}.{:08x}.tsv", kPersistStem, id);
        }

        template <class T>
        bool ParseNumber(std::string_view a_text, T& a_out, int a_base = 10)
        {
            const auto [end, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out, a_base);
            return ec == std::errc{} && end == a_text.data() + a_text.size();
        }

        // "<fnv64 hex>\t<size>\t<write time ticks>\t<path>": the path last (it may hold anything but a tab or a line end)
        bool ParseLine(std::string_view a_line, std::string& a_path, Cached& a_out)
        {
            std::string_view fields[3];
            for (auto& field : fields) {
                const auto tab = a_line.find(kField);
                if (tab == std::string_view::npos) {
                    return false;
                }
                field = a_line.substr(0, tab);
                a_line.remove_prefix(tab + 1);
            }
            fs::file_time_type::rep ticks = 0;
            if (a_line.empty() || !ParseNumber(fields[0], a_out.fnv64, 16) || !ParseNumber(fields[1], a_out.size) ||
                !ParseNumber(fields[2], ticks)) {
                return false;
            }
            a_out.written = fs::file_time_type(fs::file_time_type::duration(ticks));
            a_path.assign(a_line);
            return true;
        }

        // The last session's file, read once into the cache; a header or line that does not read drops the whole file
        void Load(Cache& a_cache)
        {
            const auto file = PersistFile();
            std::ifstream in(file ? *file : fs::path(), std::ios::binary);
            std::string line;
            if (!file || !in || !std::getline(in, line) || line != kPersistHeader) {
                return;  // none yet (or another format): everything is hashed
            }
            std::unordered_map<std::string, Cached> files;
            std::uint64_t body = NifDump::Fnv64(line.data(), line.size(), NifDump::kFnvOffset);  // the header counts
            bool ended = false;
            while (std::getline(in, line)) {
                if (line.starts_with(kPersistTrailer)) {
                    // review: a file cut short or spliced by two writers does not end with its own count and hash
                    ended = line == std::format("{}{}{}{}{:016x}", kPersistTrailer, kField, files.size(), kField, body);
                    break;
                }
                body = NifDump::Fnv64(line.data(), line.size(), body);
                std::string path;
                Cached cached;
                if (!ParseLine(line, path, cached)) {
                    logger::warn("BlackFlame: slots: {} does not read (a line is not '<fnv64>\\t<size>\\t<time>\\t<path>'): ignored, "
                                 "every Data file is hashed this session", file->filename().string());
                    return;
                }
                files[std::move(path)] = cached;
            }
            if (!ended) {
                logger::warn("BlackFlame: slots: {} does not end with its count and hash: ignored, every Data file is hashed this "
                             "session", file->filename().string());
                return;
            }
            a_cache.loaded = files.size();
            a_cache.files = std::move(files);
        }
        std::atomic<std::uint64_t> g_files{ 0 };
        std::atomic<std::uint64_t> g_bytes{ 0 };
        std::atomic<std::uint64_t> g_micros{ 0 };

        Cache& TheCache()
        {
            static auto* cache = []() {
                auto* made = std::make_unique<Cache>().release();  // leaked: worker threads may use it at exit
                Load(*made);  // the first use (kDataLoaded); a function-local static is made once, thread-safe
                return made;
            }();
            return *cache;
        }

        TrackedMutex g_persistMutex;  // one Persist at a time (the temporary file is shared)

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
                if (!it->second.confirmed) {
                    it->second.confirmed = true;  // the last session's hash, still for this size and write time
                    ++cache.reused;
                }
                return Hash{ size, it->second.fnv64 };
            }
        }
        const auto begin = std::chrono::steady_clock::now();
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
        g_files.fetch_add(1);
        g_bytes.fetch_add(read);
        g_micros.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count()));
        if (in.bad() || read != size) {
            return std::nullopt;  // review Gemini dc 9: a read cut short is no hash (and is not cached)
        }
        in.close();
        // review: replaced between the size / time read and the open (a copy renamed in): the bytes are not those of the
        // write time read first, so the hash is not cached (it outlives the session now)
        const auto sizeAfter = fs::file_size(*file, ec);
        const auto writtenAfter = ec ? fs::file_time_type{} : fs::last_write_time(*file, ec);
        if (ec || sizeAfter != size || writtenAfter != written) {
            return Hash{ read, hash };
        }
        std::lock_guard lock(cache.mutex);
        cache.files[a_dataPath] = { read, written, hash, true };
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

    Stats GetStats()
    {
        return { g_files.load(), g_bytes.load(), g_micros.load() };
    }

    void Forget(const std::string& a_dataPath)
    {
        auto& cache = TheCache();
        std::lock_guard lock(cache.mutex);
        cache.files.erase(a_dataPath);
    }

    void Persist(const char* a_when, bool a_final)
    {
        const auto file = PersistFile();
        if (!file) {
            return;
        }
        std::lock_guard persist(g_persistMutex);  // review: before the snapshot, so a later snapshot is never renamed in first
        std::string text(kPersistHeader);
        std::uint64_t body = NifDump::Fnv64(text.data(), text.size(), NifDump::kFnvOffset);
        text += '\n';
        std::uint64_t entries = 0;
        std::uint64_t loaded = 0;
        std::uint64_t reused = 0;
        {
            auto& cache = TheCache();
            std::lock_guard lock(cache.mutex);
            loaded = cache.loaded;
            reused = cache.reused;
            for (const auto& [path, cached] : cache.files) {
                // a loaded entry not checked yet is kept until the final write (the worker may still check it); then only
                // what this session saw stays (gone, or no longer used: dropped)
                if ((a_final && !cached.confirmed) || path.find_first_of("\t\r\n") != std::string::npos) {
                    continue;
                }
                const auto line = std::format("{:016x}{}{}{}{}{}{}", cached.fnv64, kField, cached.size, kField,
                    cached.written.time_since_epoch().count(), kField, path);
                body = NifDump::Fnv64(line.data(), line.size(), body);
                text += line;
                text += '\n';
                ++entries;
            }
        }
        text += std::format("{}{}{}{}{:016x}\n", kPersistTrailer, kField, entries, kField, body);
        std::error_code ec;
        fs::create_directories(file->parent_path(), ec);
        auto temp = *file;
        temp += std::format(".{}.tmp", ::GetCurrentProcessId());  // review: another game process has its own
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << text;
            out.close();
            if (!out) {
                fs::remove(temp, ec);
                logger::warn("BlackFlame: slots: {} not written ({}): the next start hashes again", file->filename().string(), a_when);
                return;
            }
        }
        fs::rename(temp, *file, ec);
        if (ec) {
            fs::remove(temp, ec);
            logger::warn("BlackFlame: slots: {} not renamed in ({}): the next start hashes again", file->filename().string(), a_when);
            return;
        }
        logger::info("BlackFlame: slots: Data file hashes kept for the next start ({}): {} entries; this session took {} of the {} "
                     "from the last one without reading them", a_when, entries, reused, loaded);
    }
}
