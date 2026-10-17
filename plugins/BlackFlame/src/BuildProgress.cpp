// =============================================================================
// BuildProgress - the progress.jsonl tail reader and the staging-to-Data copy worker (CopyContractProgress.h)
// =============================================================================

#include "BuildProgress.h"

#include "FileIndex.h"
#include "MenuWatch.h"
#include "NifDump.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace BlackFlame::BuildProgress
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";
        constexpr auto kPoll = std::chrono::milliseconds(500);
        constexpr std::size_t kReadChunk = 1 << 16;
        constexpr std::size_t kMaxLoggedErrors = 20;

        TrackedMutex g_mutex;  // the table and the stats
        std::unordered_map<std::uint64_t, Object> g_objects;
        std::set<std::string> g_inData;  // copy paths known to be in Data with their line's fnv64
        std::set<std::string> g_copyFailed;  // copies that failed once (tried again at the next start, not every poll)
        Stats g_stats;
        std::atomic<std::uint64_t> g_generation{ 0 };
        std::atomic<bool> g_workerStarted{ false };

        // The tail's place (worker thread only, and ReadAtStart before it starts)
        struct Tail
        {
            std::string pending;     // bytes after the last '\n'
            std::uint64_t offset = 0;
            std::uint64_t fileId = 0;
            bool haveFile = false;
            std::size_t errorsLogged = 0;
        };
        Tail g_tail;

        std::string Text(const fs::path& a_path)
        {
            const auto text = a_path.u8string();
            return std::string(text.begin(), text.end());
        }

        // a_file read whole: its size and FNV-1a 64 (false when it does not open)
        bool HashFile(const fs::path& a_file, std::uint64_t& a_size, std::uint64_t& a_hash)
        {
            std::ifstream in(a_file, std::ios::binary);
            if (!in) {
                return false;
            }
            a_hash = NifDump::kFnvOffset;
            a_size = 0;
            std::vector<char> chunk(kReadChunk);
            while (in.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || in.gcount() > 0) {
                const auto count = static_cast<std::size_t>(in.gcount());
                a_hash = NifDump::Fnv64(chunk.data(), count, a_hash);
                a_size += count;
            }
            return true;
        }

        bool InData(const std::string& a_path, std::uint64_t a_bytes, std::uint64_t a_fnv64)
        {
            const auto data = FileIndex::DataFolder();
            std::uint64_t size = 0;
            std::uint64_t hash = 0;
            return data && HashFile(*data / fs::path(a_path), size, hash) && size == a_bytes && hash == a_fnv64;
        }

        State StateOf(const CC::ProgressLine& a_line)
        {
            if (CC::IsTerminal(a_line.outcome)) {
                return State::kTerminal;
            }
            return CC::IsPublishable(a_line.outcome) ? State::kStaging : State::kFailed;
        }

        // A parsed line into the table (the newest line of a key wins)
        void Take(CC::ProgressLine a_line)
        {
            std::lock_guard lock(g_mutex);
            auto& object = g_objects[a_line.key];
            const bool known = object.line.key == a_line.key;
            // a compaction re-read of the same line keeps its state (kInData stays; the last line of a key wins otherwise)
            const bool same = known && object.line.path == a_line.path && object.line.fnv64 == a_line.fnv64 &&
                              object.line.outcome == a_line.outcome;
            if (!same) {
                object.state = StateOf(a_line);
            }
            object.line = std::move(a_line);
            g_stats.lines += known ? 0 : 1;
            g_generation.fetch_add(1);
        }

        // Every complete line of a_text (the part after the last '\n' kept for the next read)
        void TakeText(std::string_view a_text)
        {
            g_tail.pending.append(a_text);
            const auto end = g_tail.pending.rfind('\n');
            if (end == std::string::npos) {
                return;
            }
            const auto file = CC::ParseProgress(std::string_view(g_tail.pending).substr(0, end + 1));
            g_tail.pending.erase(0, end + 1);
            for (const auto& error : file.errors) {
                std::lock_guard lock(g_mutex);
                ++g_stats.badLines;
                if (g_tail.errorsLogged++ < kMaxLoggedErrors) {
                    logger::warn("BlackFlame: slots: progress.jsonl line {} refused: {}", error.lineNumber, error.error);
                }
            }
            for (auto line : file.lines) {
                Take(std::move(line));
            }
        }

        // Reads what was appended since the last read; a new file id (the builder compacted it) is read from its start
        void ReadNew()
        {
            const auto path = StagingFolder() / CC::kProgressFile;
            const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                return;
            }
            struct Closer
            {
                HANDLE handle;
                ~Closer() { ::CloseHandle(handle); }
            } closer{ file };
            BY_HANDLE_FILE_INFORMATION info{};
            if (!::GetFileInformationByHandle(file, &info)) {
                return;
            }
            const auto id = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
            const auto size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            if (g_tail.haveFile && (id != g_tail.fileId || size < g_tail.offset)) {
                g_tail.offset = 0;  // compacted (write-temp and rename) or cut: every line again (the table is by key)
                g_tail.pending.clear();
                std::lock_guard lock(g_mutex);
                ++g_stats.reopens;
            }
            g_tail.haveFile = true;
            g_tail.fileId = id;
            LARGE_INTEGER at{};
            at.QuadPart = static_cast<LONGLONG>(g_tail.offset);
            if (size <= g_tail.offset || !::SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
                return;
            }
            std::vector<char> chunk(kReadChunk);
            DWORD read = 0;
            while (::ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) && read > 0) {
                g_tail.offset += read;
                TakeText(std::string_view(chunk.data(), read));
            }
        }

        // staging -> Data through the VFS: the staging bytes checked against the line, a temporary file, a replacing rename,
        // the Data file read back
        bool CopyIn(const CC::ProgressLine& a_line, std::string& a_why)
        {
            const auto data = FileIndex::DataFolder();
            if (!data || !DataPathRule::ParseCopyPath(a_line.path)) {
                a_why = "no Data folder, or not a copy path";
                return false;
            }
            const auto source = StagingFolder() / fs::path(a_line.path);
            std::ifstream in(source, std::ios::binary);
            if (!in) {
                a_why = "not in staging";
                return false;
            }
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.size() != a_line.bytes || NifDump::Fnv64(bytes.data(), bytes.size()) != a_line.fnv64) {
                a_why = "the staging file is not the line's (size or fnv64)";
                return false;
            }
            const auto target = *data / fs::path(a_line.path);
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            auto temp = target;
            temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                out.flush();
                if (!out) {
                    a_why = "writing the temporary file failed";
                    return false;
                }
            }
            if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                a_why = std::format("the rename failed (Windows error {})", ::GetLastError());
                fs::remove(temp, ec);
                return false;
            }
            if (!InData(a_line.path, a_line.bytes, a_line.fnv64)) {
                a_why = "the Data file does not read back as the line's";
                return false;
            }
            return true;
        }

        // Each staging object's file into Data (while the window is open), then the objects whose copy deps are all in Data
        void Settle()
        {
            std::vector<CC::ProgressLine> todo;
            {
                std::lock_guard lock(g_mutex);
                for (const auto& [key, object] : g_objects) {
                    if (object.state == State::kStaging && !object.line.path.empty() && !g_inData.contains(object.line.path) &&
                        !g_copyFailed.contains(object.line.path)) {
                        todo.push_back(object.line);
                    }
                }
            }
            for (const auto& line : todo) {
                bool there = InData(line.path, line.bytes, line.fnv64);
                if (!there) {
                    if (!MenuWatch::PublishOpen()) {
                        std::lock_guard lock(g_mutex);
                        if (!g_stats.copyingStopped) {
                            g_stats.copyingStopped = true;
                            logger::info("BlackFlame: slots: copy worker stopped (the publish window closed: {}); what is left stays "
                                         "in staging for the next start", MenuWatch::CloseReason());
                        }
                        break;
                    }
                    std::string why;
                    there = CopyIn(line, why);
                    std::lock_guard lock(g_mutex);
                    (there ? g_stats.copied : g_stats.copyFailures) += 1;
                    if (!there) {
                        g_copyFailed.insert(line.path);
                        if (auto it = g_objects.find(line.key); it != g_objects.end() && it->second.line.path == line.path) {
                            it->second.state = State::kFailed;  // not waited for: an older generation may stand in
                            g_generation.fetch_add(1);
                        }
                        logger::warn("BlackFlame: slots: '{}' not copied into Data: {}", line.path, why);
                    }
                }
                if (there) {
                    std::lock_guard lock(g_mutex);
                    g_inData.insert(line.path);
                    g_generation.fetch_add(1);
                }
            }
            std::lock_guard lock(g_mutex);
            std::size_t inData = 0;
            for (auto& [key, object] : g_objects) {
                if (object.state == State::kStaging) {
                    const bool own = object.line.path.empty() || g_inData.contains(object.line.path);
                    const bool deps = std::all_of(object.line.deps.begin(), object.line.deps.end(), [](const CC::ProgressDep& a_dep) {
                        return a_dep.isAddn || !DataPathRule::ParseCopyPath(a_dep.path) || g_inData.contains(a_dep.path);
                    });
                    if (own && deps) {
                        object.state = State::kInData;
                        g_generation.fetch_add(1);
                    }
                }
                inData += object.state == State::kInData ? 1 : 0;
            }
            g_stats.inData = inData;
        }

        void Worker()
        {
            for (;;) {
                try {
                    ReadNew();
                    Settle();
                } catch (const std::bad_alloc&) {
                    logger::error("BlackFlame: slots: the progress worker ran out of memory reading progress.jsonl - trying again");
                } catch (const std::exception& e) {
                    logger::error("BlackFlame: slots: the progress worker threw: {}", e.what());
                } catch (...) {
                    logger::error("BlackFlame: slots: the progress worker threw an exception of an unknown type");
                }
                if (!MenuWatch::PublishOpen()) {
                    logger::info("BlackFlame: slots: progress worker done ({} keys, {} in Data, {} copied this session)",
                        GetStats().lines, GetStats().inData, GetStats().copied);
                    return;  // nothing can be published any more this session; the builder's later files wait for the next start
                }
                std::this_thread::sleep_for(kPoll);
            }
        }
    }

    void ReadAtStart()
    {
        const auto start = std::chrono::steady_clock::now();
        try {
            ReadNew();  // to its end, before the builder starts (it compacts only at its own start)
            Settle();   // the files of earlier sessions checked (and copied) now, so the groups plan from the real state
        } catch (const std::bad_alloc&) {
            logger::error("BlackFlame: slots: out of memory reading progress.jsonl at the start - its objects are not used this session");
        }
        const auto stats = GetStats();
        logger::info("BlackFlame: slots: progress.jsonl at the start: {} keys ({} in Data, {} copied now), {} lines refused{}, {} ms",
            stats.lines, stats.inData, stats.copied, stats.badLines,
            g_tail.pending.empty() ? "" : ", a last line without its end ignored",
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }

    void StartWorker()
    {
        if (g_workerStarted.exchange(true)) {
            return;
        }
        std::thread(&Worker).detach();  // detached, never joined (a thread joined at DLL unload can hang the exit)
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

    fs::path StagingFolder()
    {
        const auto log = SKSE::log::log_directory();
        return log ? *log / kFolderName / CC::kStagingFolder : fs::path();
    }

    Stats GetStats()
    {
        std::lock_guard lock(g_mutex);
        return g_stats;
    }
}
