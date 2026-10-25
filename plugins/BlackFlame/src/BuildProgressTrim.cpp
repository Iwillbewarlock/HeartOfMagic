// =============================================================================
// BuildProgressTrim - staging\published.tsv and the staging duplicates deleted (BuildProgressTrim.h)
// =============================================================================

#include "BuildProgressTrim.h"

#include "BuildProgressShared.h"
#include "CopyRequestDump.h"
#include "DataFiles.h"
#include "DiskTrimRules.h"
#include "FileIndex.h"
#include "MenuWatch.h"
#include "NifDump.h"

#include <array>
#include <chrono>
#include <fstream>
#include <mutex>
#include <random>
#include <system_error>
#include <thread>
#include <vector>

namespace BlackFlame::BuildProgress
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace CC = CopyContract;
        constexpr std::size_t kHashPiece = 1 << 20;
        constexpr std::uint64_t kMiB = 1024 * 1024;
        constexpr std::size_t kMaxLoggedFailures = 20;

        std::mutex& ListMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        CC::PublishedList& List()  // what WritePublished wrote this session
        {
            static CC::PublishedList list;
            return list;
        }

        std::uint64_t& Token()  // its token (0: none written)
        {
            static std::uint64_t token = 0;
            return token;
        }

        // A file's size and FNV-1a 64 read now (no cache: the trim deletes on it), nullopt when it does not read
        std::optional<DataFiles::Hash> HashNow(const fs::path& a_file)
        {
            std::ifstream in(a_file, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            std::vector<char> piece(kHashPiece);
            DataFiles::Hash out{ 0, NifDump::kFnvOffset };
            while (in) {
                in.read(piece.data(), static_cast<std::streamsize>(piece.size()));
                const auto got = static_cast<std::size_t>(in.gcount());
                out.fnv64 = NifDump::Fnv64(piece.data(), got, out.fnv64);
                out.size += got;
            }
            if (in.bad()) {
                return std::nullopt;
            }
            return out;
        }

        // The copies\<key16>\ folders under staging's two copies roots that are empty now. Only before the builder is
        // launched (WritePublished): a listed key can still be rebuilt (a dep gone), and a folder removed between the
        // builder's create_directories and its open would fail that write (review Codex, reviewer)
        std::size_t RemoveEmptyKeyFolders(const fs::path& a_staging)
        {
            std::size_t removed = 0;
            for (const auto root : { DataPathRule::kMeshesRoot, DataPathRule::kTexturesRoot }) {
                const auto copies = a_staging / fs::path(std::string(root) + std::string(DataPathRule::kCopiesFolder));
                std::error_code ec;
                std::vector<fs::path> empty;
                for (fs::directory_iterator it(copies, ec), end; !ec && it != end; it.increment(ec)) {
                    std::error_code typeEc;
                    if (it->is_directory(typeEc) && !it->is_symlink(typeEc) && fs::is_empty(it->path(), typeEc) && !typeEc) {
                        empty.push_back(it->path());
                    }
                }
                for (const auto& folder : empty) {
                    std::error_code removeEc;
                    removed += fs::remove(folder, removeEc) ? 1 : 0;  // remove: an empty folder only, never remove_all
                }
            }
            return removed;
        }

        void Trim(std::set<std::string> a_keep)
        {
            ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const auto start = std::chrono::steady_clock::now();
            const auto list = Published();
            const auto staging = StagingFolder();
            const auto data = FileIndex::DataFolder();
            std::size_t removed = 0;
            std::size_t keptNamed = 0;
            std::size_t differs = 0;
            std::size_t failures = 0;
            std::uint64_t bytes = 0;
            bool stopped = false;
            if (staging.empty() || !data) {
                return;
            }
            for (const auto& [path, file] : list) {
                const auto staged = staging / fs::path(path);
                std::error_code ec;
                if (!fs::exists(staged, ec) || ec) {
                    continue;  // trimmed by an earlier start
                }
                if (!DiskTrimRules::MayTrim(path, list, a_keep)) {
                    ++keptNamed;
                    continue;
                }
                if (!MenuWatch::PublishOpen()) {
                    stopped = true;  // a load began: the rest at the next start
                    break;
                }
                const auto now = HashNow(*data / fs::path(path));
                if (!now || now->size != file.bytes || now->fnv64 != file.fnv64) {
                    ++differs;  // Data changed since the list (or does not read): the staging file stays
                    continue;
                }
                // reviewer: only a true duplicate goes - a staging file with other bytes (another build of the key, the builder
                // rewriting it now) is what a line stands on, and the worker may still copy it
                const auto stagedNow = HashNow(staged);
                if (!stagedNow || stagedNow->size != file.bytes || stagedNow->fnv64 != file.fnv64) {
                    ++differs;
                    continue;
                }
                if (!fs::remove(staged, ec) || ec) {
                    if (failures++ < kMaxLoggedFailures) {
                        logger::warn("BlackFlame: disk trim: '{}' not deleted from staging: {}", path, ec.message());
                    }
                    continue;
                }
                ++removed;
                bytes += stagedNow->size;
            }
            logger::info("BlackFlame: disk trim: {} staging duplicate(s) of Data files deleted ({} MiB); "
                         "{} kept because a source names them, {} kept because Data or staging differs now, {} failed{}; {} ms",
                removed, bytes / kMiB, keptNamed, differs, failures,
                stopped ? "; stopped: a load began (the rest at the next start)" : "",
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        }
    }

    bool WritePublished()
    {
        const auto staging = StagingFolder();
        if (staging.empty()) {
            return false;
        }
        const auto file = staging / CC::kPublishedFile;
        {
            std::lock_guard lock(ListMutex());
            List().clear();
            Token() = 0;
        }
        std::error_code ec;
        // the folders the last session's trim emptied (the builder is not running yet)
        if (const auto folders = RemoveEmptyKeyFolders(staging); folders > 0) {
            logger::info("BlackFlame: disk trim: {} empty staging folder(s) removed", folders);
        }
        fs::remove(file, ec);  // an old list never vouches for this start's Data
        if (ec) {
            logger::warn("BlackFlame: disk trim: the last published.tsv could not be removed ({}): no list this start", ec.message());
            return false;
        }
        std::vector<CC::ProgressLine> lines;
        {
            auto& s = Detail::The();
            std::lock_guard lock(s.mutex);
            lines.reserve(s.objects.size());
            for (const auto& [key, object] : s.objects) {
                lines.push_back(object.line);
            }
        }
        CC::PublishedList list;
        for (const auto& line : lines) {
            if (CC::IsPublishable(line.outcome) && DataPathRule::ParseCopyPath(line.path) &&
                DataFiles::Matches(line.path, line.bytes, line.fnv64)) {
                list.insert_or_assign(line.path, CC::PublishedFile{ line.bytes, line.fnv64 });
            }
        }
        // the deps after the own files: a path some line names as its own file is that line's, listed or not (reviewer: a
        // dep's other build of it must never list it, or the trim would delete the line's own staging file)
        std::set<std::string> owned;
        for (const auto& line : lines) {
            if (!line.path.empty()) {
                owned.insert(line.path);
            }
        }
        for (const auto& line : lines) {
            if (!CC::IsPublishable(line.outcome)) {
                continue;
            }
            for (const auto& dep : line.deps) {
                if (dep.isAddn || owned.contains(dep.path) || list.contains(dep.path) || !DataPathRule::ParseCopyPath(dep.path)) {
                    continue;
                }
                if (const auto hash = DataFiles::HashOf(dep.path); hash && hash->fnv64 == dep.fnv64 && hash->size > 0) {
                    list.emplace(dep.path, CC::PublishedFile{ hash->size, hash->fnv64 });
                }
            }
        }
        // a new token each start, also in request.json: a list an earlier start left behind never matches (review Codex)
        std::random_device random;
        const std::uint64_t token = ((static_cast<std::uint64_t>(random()) << 32) ^ random() ^
                                        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())) | 1;
        const auto text = CC::WritePublished(list, token);
        if (!text) {
            logger::warn("BlackFlame: disk trim: published.tsv refused by the contract's writer: no list this start");
            return false;
        }
        try {
            fs::create_directories(staging, ec);
            CopyRequestDump::WriteWhole(file, *text);
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: disk trim: writing published.tsv failed ({}): no list this start", e.what());
            return false;
        }
        logger::info("BlackFlame: disk trim: published.tsv lists {} file(s) Data holds exactly", list.size());
        std::lock_guard lock(ListMutex());
        List() = std::move(list);
        Token() = token;
        return true;
    }

    std::optional<std::uint64_t> PublishedToken()
    {
        std::lock_guard lock(ListMutex());
        return Token() == 0 ? std::nullopt : std::optional(Token());
    }

    CC::PublishedList Published()
    {
        std::lock_guard lock(ListMutex());
        return List();
    }

    std::optional<CC::ProgressLine> LastLine(std::uint64_t a_key)
    {
        auto& s = Detail::The();
        std::lock_guard lock(s.mutex);
        const auto it = s.objects.find(a_key);
        return it == s.objects.end() ? std::nullopt : std::optional(it->second.line);
    }

    std::optional<CC::PublishedFile> StagedHash(const std::string& a_path)
    {
        const auto staging = StagingFolder();
        std::error_code ec;
        if (staging.empty() || !fs::is_regular_file(staging / fs::path(a_path), ec) || ec) {
            return std::nullopt;
        }
        const auto hash = HashNow(staging / fs::path(a_path));
        return hash ? std::optional(CC::PublishedFile{ hash->size, hash->fnv64 }) : std::nullopt;
    }

    void StartTrim(std::set<std::string> a_keep)
    {
        std::thread([keep = std::move(a_keep)]() mutable {
            try {
                Trim(std::move(keep));
            } catch (const std::exception& e) {
                logger::error("BlackFlame: disk trim: the staging trim threw ({}); the rest at the next start", e.what());
            } catch (...) {
                logger::error("BlackFlame: disk trim: the staging trim threw an exception of an unknown type");
            }
        }).detach();  // detached, never joined (as the progress worker)
    }
}
