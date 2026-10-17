// =============================================================================
// BuildProgress - the progress.jsonl tail reader and the staging-to-Data copy worker (CopyContractProgress.h)
// =============================================================================

#include "BuildProgress.h"

#include "BuilderLauncher.h"
#include "DataFiles.h"
#include "FileIndex.h"
#include "MenuWatch.h"
#include "NifDump.h"
#include "ProgressTail.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>
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

        using PathHash = std::pair<std::string, std::uint64_t>;  // a copy path and the fnv64 it must hold

        // Everything the worker and the readers share; leaked on purpose (the detached worker may run while statics go)
        struct Shared
        {
            TrackedMutex mutex;  // the members below but tail (the worker's, and ReadAtStart's before it starts)
            std::unordered_map<std::uint64_t, Object> objects;  // by key: the last line of a key wins
            std::set<PathHash> inData;                          // files known to be in Data with that fnv64
            std::set<PathHash> copyFailed;                      // copies that failed once (tried again at the next start)
            std::set<PathHash> copiedNow;                       // copied into Data this session
            std::set<PathHash> replacedNow;                     // of them, over another file at that path
            Stats stats;
            ProgressTail::Tail tail;
            std::size_t errorsLogged = 0;
        };
        Shared& The()
        {
            static auto* shared = std::make_unique<Shared>().release();
            return *shared;
        }
        std::atomic<std::uint64_t> g_generation{ 0 };
        std::atomic<bool> g_workerStarted{ false };
        std::atomic<bool> g_caughtUp{ false };

        State StateOf(const CC::ProgressLine& a_line)
        {
            if (CC::IsTerminal(a_line.outcome)) {
                return State::kTerminal;
            }
            return CC::IsPublishable(a_line.outcome) ? State::kStaging : State::kFailed;
        }

        // A parsed line into the table; a re-read of the same line (the compaction) keeps its state
        void Take(CC::ProgressLine a_line)
        {
            auto& s = The();
            std::lock_guard lock(s.mutex);
            const auto [it, added] = s.objects.try_emplace(a_line.key);
            auto& object = it->second;
            const bool known = !added;
            const bool same = known && object.line == a_line;  // every field, the deps included (review Gemini 1)
            if (!same) {
                object.state = StateOf(a_line);
            }
            object.line = std::move(a_line);
            s.stats.lines += known ? 0 : 1;
            g_generation.fetch_add(1);
        }

        void TakeLines(const std::string& a_text)
        {
            if (a_text.empty()) {
                return;
            }
            const auto file = CC::ParseProgress(a_text);
            auto& s = The();
            for (const auto& error : file.errors) {
                std::lock_guard lock(s.mutex);
                ++s.stats.badLines;
                if (s.errorsLogged++ < kMaxLoggedErrors) {
                    logger::warn("BlackFlame: slots: a progress.jsonl line refused: {}", error.error);
                }
            }
            for (auto line : file.lines) {
                Take(std::move(line));
            }
        }

        // What was appended since the last read (ProgressTail: a new file id is read from 0, a cut last line again next time)
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
            auto& tail = The().tail;
            bool reset = false;
            const auto id = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
            const auto size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            const auto from = ProgressTail::Begin(tail, id, size, reset);
            if (reset) {
                std::lock_guard lock(The().mutex);
                ++The().stats.reopens;
            }
            LARGE_INTEGER at{};
            at.QuadPart = static_cast<LONGLONG>(from);
            if (size <= from || !::SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
                return;
            }
            try {
                std::vector<char> chunk(kReadChunk);
                DWORD read = 0;
                while (::ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) && read > 0) {
                    TakeLines(ProgressTail::Take(tail, std::string_view(chunk.data(), read)));
                }
            } catch (...) {
                tail = {};  // lines may be lost past the offset: read the file again from 0 (a line read twice changes nothing)
                throw;
            }
            ProgressTail::End(tail);
        }

        // staging -> Data through the VFS: the staging bytes checked against the line, a temporary file, a replacing rename,
        // the Data file read back. Copies, never moves (the builder's compaction and cache need the staging files)
        bool CopyIn(const CC::ProgressLine& a_line, bool& a_replaced, std::string& a_why)
        {
            const auto data = FileIndex::DataFolder();
            if (!data || !DataPathRule::ParseCopyPath(a_line.path)) {
                a_why = "no Data folder, or not a copy path";
                return false;
            }
            std::ifstream in(StagingFolder() / fs::path(a_line.path), std::ios::binary);
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
            if (ec) {
                a_why = "its Data folder could not be made: " + ec.message();
                return false;
            }
            auto temp = target;
            temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                out.flush();
                if (!out) {
                    a_why = "writing the temporary file failed";
                    out.close();
                    fs::remove(temp, ec);
                    return false;
                }
            }
            if (!MenuWatch::PublishOpen()) {  // closed while the bytes were written: nothing enters Data during a load
                a_why = "the publish window closed during the copy";
                fs::remove(temp, ec);
                return false;
            }
            a_replaced = fs::exists(target, ec) || ec;  // a file there now is not the line's (Settle found no match)
            if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                a_why = std::format("the rename failed (Windows error {})", ::GetLastError());
                fs::remove(temp, ec);
                return false;
            }
            DataFiles::Forget(a_line.path);
            if (!DataFiles::Matches(a_line.path, a_line.bytes, a_line.fnv64)) {
                a_why = "the Data file does not read back as the line's";
                return false;
            }
            return true;
        }

        // Each staging object's file found in Data or (a_copy, the window open) copied in; then the objects whose own file and
        // copy deps are all in Data become kInData. A copy that fails marks its object failed (an older generation stands in)
        void Settle(bool a_copy)
        {
            auto& s = The();
            std::vector<CC::ProgressLine> todo;
            {
                std::lock_guard lock(s.mutex);
                for (const auto& [key, object] : s.objects) {
                    const PathHash file{ object.line.path, object.line.fnv64 };
                    if (object.state == State::kStaging && !object.line.path.empty() && !s.inData.contains(file) &&
                        !s.copyFailed.contains(file)) {
                        todo.push_back(object.line);
                    }
                }
            }
            for (const auto& line : todo) {
                bool there = DataFiles::Matches(line.path, line.bytes, line.fnv64);
                bool copied = false;
                if (!there && a_copy) {
                    if (!MenuWatch::PublishOpen()) {
                        std::lock_guard lock(s.mutex);
                        if (!s.stats.copyingStopped) {
                            s.stats.copyingStopped = true;
                            logger::info("BlackFlame: slots: copy worker stopped (the publish window closed: {}); what is left stays "
                                         "in staging for the next start", MenuWatch::CloseReason());
                        }
                        break;
                    }
                    std::string why;
                    bool replaced = false;
                    copied = there = CopyIn(line, replaced, why);
                    if (copied && replaced) {
                        std::lock_guard lock(s.mutex);
                        s.replacedNow.insert({ line.path, line.fnv64 });
                    }
                    std::lock_guard lock(s.mutex);
                    (there ? s.stats.copied : s.stats.copyFailures) += 1;
                    if (!there) {
                        s.copyFailed.insert({ line.path, line.fnv64 });
                        if (auto it = s.objects.find(line.key); it != s.objects.end() && it->second.line.path == line.path) {
                            it->second.state = State::kFailed;
                            g_generation.fetch_add(1);
                        }
                        logger::warn("BlackFlame: slots: '{}' not copied into Data: {}", line.path, why);
                    }
                }
                if (there) {
                    std::lock_guard lock(s.mutex);
                    s.inData.insert({ line.path, line.fnv64 });
                    if (copied) {
                        s.copiedNow.insert({ line.path, line.fnv64 });  // by file, so a later line of the key keeps it
                    }
                    g_generation.fetch_add(1);
                }
            }
            // the objects whose own file is in Data; their copy deps checked without the lock (a hash may read a file)
            std::vector<CC::ProgressLine> ready;
            {
                std::lock_guard lock(s.mutex);
                for (auto& [key, object] : s.objects) {
                    if (object.state != State::kStaging) {
                        continue;
                    }
                    // review agy 3: a file whose copy failed fails every object that is it or needs it (another key's line
                    // for the same file is skipped by the copy loop and would wait for the whole session)
                    const auto failed = [&](const std::string& a_path, std::uint64_t a_fnv) { return s.copyFailed.contains({ a_path, a_fnv }); };
                    if ((!object.line.path.empty() && failed(object.line.path, object.line.fnv64)) ||
                        std::any_of(object.line.deps.begin(), object.line.deps.end(),
                            [&](const CC::ProgressDep& a_dep) { return !a_dep.isAddn && failed(a_dep.path, a_dep.fnv64); })) {
                        object.state = State::kFailed;
                        g_generation.fetch_add(1);
                        continue;
                    }
                    if (object.line.path.empty() || s.inData.contains({ object.line.path, object.line.fnv64 })) {
                        ready.push_back(object.line);
                    }
                }
            }
            const auto depInData = [&](const CC::ProgressDep& a_dep) {
                if (a_dep.isAddn || !DataPathRule::ParseCopyPath(a_dep.path)) {
                    return true;  // an ADDN dep is CopyGroups'; a source file is the builder's input (the key covers it)
                }
                {
                    std::lock_guard lock(s.mutex);
                    if (s.inData.contains({ a_dep.path, a_dep.fnv64 })) {
                        return true;
                    }
                }
                const auto hash = DataFiles::HashOf(a_dep.path);
                return hash && hash->fnv64 == a_dep.fnv64;
            };
            std::vector<const CC::ProgressLine*> complete;
            for (const auto& line : ready) {
                if (std::all_of(line.deps.begin(), line.deps.end(), depInData)) {
                    complete.push_back(&line);
                }
            }
            std::lock_guard lock(s.mutex);
            for (const auto* line : complete) {  // still that line (a newer one for the key starts over)
                if (auto it = s.objects.find(line->key); it != s.objects.end() && it->second.state == State::kStaging &&
                    it->second.line.path == line->path && it->second.line.fnv64 == line->fnv64) {
                    it->second.state = State::kInData;
                    g_generation.fetch_add(1);
                }
            }
            std::size_t inData = 0;
            for (const auto& [key, object] : s.objects) {
                inData += object.state == State::kInData ? 1 : 0;
            }
            s.stats.inData = inData;
        }

        void Worker()
        {
            std::size_t failures = 0;
            for (;;) {
                try {
                    const bool builderDone = !BuilderLauncher::Running();  // before the read: what it wrote is all there
                    ReadNew();
                    Settle(true);
                    if (builderDone && !g_caughtUp.exchange(true)) {
                        g_generation.fetch_add(1);  // CopyGroups re-plans: keys without a line are left now
                        logger::info("BlackFlame: slots: progress caught up after the builder ({}): {} keys, {} in Data",
                            BuilderLauncher::Status(), GetStats().lines, GetStats().inData);
                    }
                } catch (const std::exception& e) {
                    if (failures++ < kMaxLoggedErrors) {  // review agy 12: a repeated failure is not logged every poll
                        logger::error("BlackFlame: slots: the progress worker threw ({}) - trying again at the next poll{}", e.what(),
                            failures == kMaxLoggedErrors ? "; further failures are not logged" : "");
                    }
                } catch (...) {
                    if (failures++ < kMaxLoggedErrors) {
                        logger::error("BlackFlame: slots: the progress worker threw an exception of an unknown type - trying again");
                    }
                }
                if (!MenuWatch::PublishOpen()) {
                    logger::info("BlackFlame: slots: progress worker done ({} keys, {} in Data, {} copied this session)",
                        GetStats().lines, GetStats().inData, GetStats().copied);
                    return;  // nothing can be published any more this session; later files wait for the next start
                }
                std::this_thread::sleep_for(kPoll);
            }
        }
    }

    void ReadAtStart()
    {
        const auto start = std::chrono::steady_clock::now();
        try {
            ReadNew();       // to its end, before the builder starts (it compacts only at its own start)
            Settle(false);   // what earlier sessions copied into Data counts now; copying is the worker's, after W1
        } catch (const std::exception& e) {
            logger::error("BlackFlame: slots: reading progress.jsonl at the start failed ({}) - the worker tries again", e.what());
        } catch (...) {
            logger::error("BlackFlame: slots: reading progress.jsonl at the start threw an exception of an unknown type - the "
                          "worker tries again");
        }
        const auto stats = GetStats();
        logger::info("BlackFlame: slots: progress.jsonl at the start: {} keys ({} in Data), {} lines refused, {} ms", stats.lines,
            stats.inData, stats.badLines,
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
        auto& s = The();
        std::lock_guard lock(s.mutex);
        const auto it = s.objects.find(a_key);
        if (it == s.objects.end()) {
            return std::nullopt;
        }
        auto object = it->second;
        const PathHash file{ object.line.path, object.line.fnv64 };
        object.copiedThisSession = s.copiedNow.contains(file);
        object.replacedThisSession = s.replacedNow.contains(file);
        return object;
    }

    std::uint64_t Generation() noexcept
    {
        return g_generation.load();
    }

    bool CaughtUp() noexcept
    {
        return g_caughtUp.load();
    }

    fs::path StagingFolder()
    {
        const auto log = SKSE::log::log_directory();
        return log ? *log / kFolderName / CC::kStagingFolder : fs::path();
    }

    Stats GetStats()
    {
        auto& s = The();
        std::lock_guard lock(s.mutex);
        return s.stats;
    }
}
