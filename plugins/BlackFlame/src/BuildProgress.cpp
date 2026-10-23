// =============================================================================
// BuildProgress - the progress.jsonl tail reader and the staging-to-Data copy worker (CopyContractProgress.h)
// =============================================================================

#include "BuildProgress.h"

#include "BuildProgressCopy.h"
#include "BuilderLauncher.h"
#include "DataFiles.h"
#include "FileIndex.h"
#include "MenuWatch.h"
#include "ProgressTail.h"
#include "SlotLedger.h"
#include "TrackedMutex.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <system_error>
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
            std::set<PathHash> inData;                          // files known to be in Data with that fnv64 (or kept for it)
            std::set<PathHash> copyFailed;                      // copies that failed once (tried again at the next start)
            std::set<PathHash> copiedNow;                       // copied into Data this session
            std::set<PathHash> replacedNow;                     // of them, over another file at that path
            std::map<PathHash, DataFiles::Hash> kept;           // a line's file whose path holds the ledger's build of its key (KeepPublished)
            Stats stats;
            ProgressTail::Tail tail;
            std::size_t errorsLogged = 0;
            std::size_t copyWarnings = 0;  // "not copied into Data" lines logged (at most kMaxLoggedErrors)
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
        enum class Read : std::uint8_t
        {
            kAbsent,  // no progress file (the builder wrote nothing yet)
            kRead,    // read to its end
            kFailed,  // it is there but could not be read: tried again at the next poll, never "caught up" on it
        };

        Read ReadNew()
        {
            const auto path = StagingFolder() / CC::kProgressFile;
            const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                const auto error = ::GetLastError();  // review Codex 2: absent and unreadable differ
                return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Read::kAbsent : Read::kFailed;
            }
            struct Closer
            {
                HANDLE handle;
                ~Closer() { ::CloseHandle(handle); }
            } closer{ file };
            BY_HANDLE_FILE_INFORMATION info{};
            if (!::GetFileInformationByHandle(file, &info)) {
                return Read::kFailed;
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
            if (size <= from) {
                return Read::kRead;  // nothing new
            }
            if (!::SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
                return Read::kFailed;
            }
            bool ended = false;
            try {
                std::vector<char> chunk(kReadChunk);
                DWORD read = 0;
                for (;;) {
                    if (!::ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) {
                        break;  // an I/O failure: not the end
                    }
                    if (read == 0) {
                        ended = true;
                        break;
                    }
                    TakeLines(ProgressTail::Take(tail, std::string_view(chunk.data(), read)));
                }
            } catch (...) {
                tail = {};  // lines may be lost past the offset: read the file again from 0 (a line read twice changes nothing)
                throw;
            }
            if (!ended) {
                tail = {};  // where the failure left off is unsure: from 0 at the next poll
                return Read::kFailed;
            }
            ProgressTail::End(tail);
            return Read::kRead;
        }

        // review round 1 D1: with the GPU encoder a rebuild of a key (its staging file lost, another encoder mix) gives other
        // bytes at the same copy path. When the Data file there is one a published object of the ledger stands on, it is kept,
        // not deferred (ApplyDeferred would replace it at the next start and every slot on it would show its source for that
        // session), and stands for the line's file: dependants find the line's fnv64 in inData, and Find names the Data file's
        // size and fnv64, so what is verified and recorded in the ledger is what is in Data. Each path once a session (inData
        // then holds it)
        //
        // (reviewer round 2) The ledger is looked up by path before the Data file is hashed: DataFiles::Matches stops at a
        // size that differs without reading the file, so only a path a published object names is read whole here.
        // a_published is the ledger's published files (SlotLedgerModel::PublishedFiles), made on first use: one ledger copy
        // a pass, not one a line
        std::optional<DataFiles::Hash> PublishedDataFile(const std::string& a_path,
            std::optional<SlotLedgerModel::PublishedFileMap>& a_published)
        {
            if (!a_published) {
                a_published = SlotLedgerModel::PublishedFiles(SlotLedger::Copy());
            }
            const auto recorded = a_published->find(a_path);
            if (recorded == a_published->end()) {
                return std::nullopt;
            }
            const auto hash = DataFiles::HashOf(a_path);
            if (!hash || !recorded->second.contains(hash->fnv64)) {
                return std::nullopt;
            }
            return hash;
        }

        bool KeepPublished(const CC::ProgressLine& a_line, std::optional<SlotLedgerModel::PublishedFileMap>& a_published)
        {
            if (!DataPathRule::ParseCopyPath(a_line.path)) {
                return false;
            }
            const auto hash = PublishedDataFile(a_line.path, a_published);
            if (!hash) {
                return false;
            }
            auto& s = The();
            {
                std::lock_guard lock(s.mutex);
                s.kept[{ a_line.path, a_line.fnv64 }] = *hash;
                ++s.stats.kept;
            }
            logger::info("BlackFlame: slots: '{}': kept the published copy (another build of the same key; fnv64 {} in Data, {} in "
                         "staging)", a_line.path, CC::Hex16(hash->fnv64), CC::Hex16(a_line.fnv64));
            return true;
        }

        // Each staging object's file found in Data (or kept: KeepPublished) or (a_copy, the window open) copied in; then the
        // objects whose own file and copy deps are all in Data become kInData. A copy that fails marks its object failed (an
        // older generation stands in)
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
            std::optional<SlotLedgerModel::PublishedFileMap> published;  // made by the first line Matches refuses
            for (const auto& line : todo) {
                bool there = DataFiles::Matches(line.path, line.bytes, line.fnv64) || KeepPublished(line, published);
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
                    bool deferred = false;
                    copied = there = CopyIn(line, deferred, why);
                    std::lock_guard lock(s.mutex);
                    (there ? s.stats.copied : s.stats.copyFailures) += 1;
                    if (!there) {
                        s.copyFailed.insert({ line.path, line.fnv64 });
                        if (auto it = s.objects.find(line.key); it != s.objects.end() && it->second.line.path == line.path) {
                            it->second.state = State::kFailed;
                            g_generation.fetch_add(1);
                        }
                        if (deferred) {
                            logger::info("BlackFlame: slots: '{}': {}", line.path, why);
                        } else if (s.copyWarnings++ < kMaxLoggedErrors) {
                            logger::warn("BlackFlame: slots: '{}' not copied into Data: {}{}", line.path, why,
                                s.copyWarnings == kMaxLoggedErrors ? " (further failed copies are counted, not logged)" : "");
                        }
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

        // ReadAtStart, after Settle(false) (reviewer round 1 of the old-copies cleanup): a staging line whose file is neither
        // in Data nor in staging any more (the cleanup removed its key folders, or staging was cleared) is dropped from the
        // table, as if it had no line: copying it would fail once a line, and its copyFailed entry would pin the key kFailed
        // when the builder writes the same line again this session (Take keeps a same line's state). A line whose staging
        // file cannot be looked at stays. The builder's compaction drops such lines from the file at its start
        std::size_t DropUnstaged()
        {
            auto& s = The();
            std::vector<std::pair<std::uint64_t, std::string>> candidates;
            {
                std::lock_guard lock(s.mutex);
                for (const auto& [key, object] : s.objects) {
                    if (object.state == State::kStaging && !object.line.path.empty() &&
                        !s.inData.contains({ object.line.path, object.line.fnv64 })) {
                        candidates.emplace_back(key, object.line.path);
                    }
                }
            }
            std::size_t dropped = 0;
            for (const auto& [key, path] : candidates) {
                std::error_code ec;
                if (fs::exists(StagingFolder() / fs::path(path), ec) || ec) {
                    continue;
                }
                std::lock_guard lock(s.mutex);
                if (const auto it = s.objects.find(key); it != s.objects.end() && it->second.state == State::kStaging &&
                                                         it->second.line.path == path) {
                    s.objects.erase(it);
                    --s.stats.lines;
                    ++dropped;
                }
            }
            if (dropped > 0) {
                g_generation.fetch_add(1);
            }
            return dropped;
        }

        void Worker()
        {
            std::size_t failures = 0;
            for (;;) {
                try {
                    const bool builderDone = !BuilderLauncher::Running();  // before the read: what it wrote is all there
                    const auto read = ReadNew();
                    Settle(true);
                    if (read == Read::kFailed && failures++ < kMaxLoggedErrors) {
                        logger::warn("BlackFlame: slots: progress.jsonl could not be read - tried again at the next poll");
                    }
                    // review Codex 2: caught up only after a read that reached the end (or found no file)
                    if (builderDone && read != Read::kFailed && !g_caughtUp.exchange(true)) {
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
                    DataFiles::Persist("progress worker done");  // the copies hashed after kDataLoaded too
                    logger::info("BlackFlame: slots: progress worker done ({} keys, {} in Data, {} copied this session, {} published "
                                 "copies kept)", GetStats().lines, GetStats().inData, GetStats().copied, GetStats().kept);
                    return;  // nothing can be published any more this session; later files wait for the next start
                }
                std::this_thread::sleep_for(kPoll);
            }
        }
    }

    void ReadAtStart()
    {
        const auto start = std::chrono::steady_clock::now();
        std::size_t dropped = 0;
        try {
            if (ReadNew() == Read::kFailed) {  // to its end, before the builder starts (it compacts only at its own start)
                logger::warn("BlackFlame: slots: progress.jsonl could not be read at the start - the worker tries again");
            }
            Settle(false);   // what earlier sessions copied into Data counts now; copying is the worker's, after W1
            dropped = DropUnstaged();
        } catch (const std::exception& e) {
            logger::error("BlackFlame: slots: reading progress.jsonl at the start failed ({}) - the worker tries again", e.what());
        } catch (...) {
            logger::error("BlackFlame: slots: reading progress.jsonl at the start threw an exception of an unknown type - the "
                          "worker tries again");
        }
        const auto stats = GetStats();
        logger::info("BlackFlame: slots: progress.jsonl at the start: {} keys ({} in Data), {} lines refused, {} dropped (their "
                     "file neither in Data nor in staging), {} ms", stats.lines, stats.inData, stats.badLines, dropped,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }

    void ApplyDeferred()
    {
        // review N3: copies an earlier session put beside a file of their path, renamed in now, before anything uses the
        // path this session; the engine's index may still hold the old file's size, so they count as replaced (used from
        // the next start)
        std::error_code ec;
        const auto list = DeferredList();
        const auto data = FileIndex::DataFolder();
        if (!data || !fs::exists(list, ec)) {
            return;
        }
        std::ifstream in(list, std::ios::binary);
        std::string row;
        std::size_t renamed = 0;
        std::size_t dropped = 0;
        std::size_t keptPublished = 0;  // of the dropped, over a file a published object stands on (deleted)
        std::optional<SlotLedgerModel::PublishedFileMap> published;  // SlotLedger::Load ran before OnDataLoaded (Main.cpp)
        auto& s = The();
        while (std::getline(in, row)) {
            const auto tab1 = row.find('\t');
            const auto tab2 = tab1 == std::string::npos ? std::string::npos : row.find('\t', tab1 + 1);
            const auto path = row.substr(0, tab1);
            const auto fnv = tab2 == std::string::npos ? std::nullopt : CC::ParseHex16(row.substr(tab2 + 1));
            std::uint64_t bytes = 0;
            const bool sized = tab2 != std::string::npos &&
                               std::from_chars(row.data() + tab1 + 1, row.data() + tab2, bytes).ec == std::errc{};
            if (!sized || !fnv || !DataPathRule::ParseCopyPath(path)) {
                ++dropped;
                continue;
            }
            const auto target = *data / fs::path(path);
            auto next = target;
            next += kNextSuffix;
            if (fs::file_size(next, ec) != bytes || ec) {
                ++dropped;
                ec.clear();
                continue;
            }
            // (reviewer round 2) KeepPublished's rule at this door too: a .next (another build of the key, deferred by an
            // earlier session or an earlier DLL) never replaces a Data file a published ledger object stands on, or every
            // slot on it would show its source colour for this session (review round 1 D1's launch N+1)
            if (const auto held = PublishedDataFile(path, published)) {
                fs::remove(next, ec);
                logger::info("BlackFlame: slots: '{}': deferred copy dropped, the published copy kept (fnv64 {} in Data, {} "
                             "deferred){}", path, CC::Hex16(held->fnv64), CC::Hex16(*fnv), ec ? "; the .next was not deleted" : "");
                ec.clear();
                ++dropped;
                ++keptPublished;
                continue;
            }
            if (!::MoveFileExW(next.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                ++dropped;
                continue;
            }
            DataFiles::Forget(path);
            std::lock_guard lock(s.mutex);
            s.copiedNow.insert({ path, *fnv });
            s.replacedNow.insert({ path, *fnv });
            ++renamed;
        }
        in.close();
        fs::remove(list, ec);
        logger::info("BlackFlame: slots: {} deferred cop{} renamed into Data ({} dropped: no file of that size beside it, a bad "
                     "line, or {} over a published copy); used from the next start", renamed, renamed == 1 ? "y" : "ies", dropped,
            keptPublished);
    }

    bool ReplacedNow(const std::string& a_path, std::uint64_t a_fnv64)
    {
        auto& s = The();
        std::lock_guard lock(s.mutex);
        return s.replacedNow.contains({ a_path, a_fnv64 });
    }

    bool CopiedNow(const std::string& a_path, std::uint64_t a_fnv64)
    {
        auto& s = The();
        std::lock_guard lock(s.mutex);
        return s.copiedNow.contains({ a_path, a_fnv64 });
    }

    void StartWorker()
    {
        if (g_workerStarted.exchange(true)) {
            return;
        }
        std::thread(&Worker).detach();  // detached, never joined (a thread joined at DLL unload can hang the exit)
    }

    void SkipWorker()
    {
        if (g_workerStarted.exchange(true) || g_caughtUp.exchange(true)) {
            return;
        }
        g_generation.fetch_add(1);  // CopyGroups re-plans: keys without a line, and staging objects, are left now
        logger::info("BlackFlame: slots: no progress worker this session (another game holds the request folder): {} keys read at "
                     "the start, {} in Data", GetStats().lines, GetStats().inData);
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
        // review round 1 D1: a kept file (KeepPublished) by the Data file's size and fnv64, its own and as a dep, so the
        // object CopyGroups verifies and records is the one in Data (ObjectIntact at the next start compares those)
        if (const auto own = s.kept.find({ object.line.path, object.line.fnv64 }); own != s.kept.end()) {
            object.line.bytes = own->second.size;
            object.line.fnv64 = own->second.fnv64;
        }
        for (auto& dep : object.line.deps) {
            if (const auto kept = dep.isAddn ? s.kept.end() : s.kept.find({ dep.path, dep.fnv64 }); kept != s.kept.end()) {
                dep.fnv64 = kept->second.fnv64;
            }
        }
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
