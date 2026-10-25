// =============================================================================
// GreyProgress - progress.jsonl (append-only, one write per line; compacted at the builder's start) and the staging files
// =============================================================================
// Appended lines are not flushed one by one: a line lost to a crash or power cut only rebuilds its object. What makes a kept
// line safe is the compaction's check that its file is in staging with the line's bytes and fnv64 (StagedMatches); the
// compacted file itself is flushed before its rename.
#include "GreyProgress.h"

#include "GreyBuild.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;

        constexpr std::size_t kReadPiece = 1u << 20;  // 1 MiB a read
        constexpr int kDecimalBase = 10;
        constexpr unsigned char kFirstNonAscii = 0x80;

        // The whole file; nullopt when it is there but does not read (then it is left as it is)
        [[nodiscard]] std::optional<std::string> ReadText(const std::filesystem::path& a_file, bool& a_exists)
        {
            std::error_code ec;
            a_exists = std::filesystem::exists(a_file, ec);
            if (ec) {
                return std::nullopt;
            }
            if (!a_exists) {
                return std::string();
            }
            std::ifstream in(a_file, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            std::string text(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
            if (in.bad()) {
                return std::nullopt;
            }
            return text;
        }

        // FILE_RENAME_INFO with its Win10 flags field (the SDK names it only for newer _WIN32_WINNT targets): same layout
        struct RenameInfoEx
        {
            DWORD flags;
            HANDLE rootDirectory;
            DWORD fileNameLength;
            WCHAR fileName[1];
        };
        constexpr DWORD kRenameReplaceIfExists = 0x1;  // FILE_RENAME_FLAG_REPLACE_IF_EXISTS
        constexpr DWORD kRenamePosixSemantics = 0x2;   // FILE_RENAME_FLAG_POSIX_SEMANTICS
        constexpr auto kFileRenameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);  // FileRenameInfoEx (Windows 10 1709+)

        // a_temp over a_target with POSIX semantics: it replaces a target another process holds open with FILE_SHARE_DELETE
        // (the DLL's tail reader on progress.jsonl), which MoveFileExW refuses with ERROR_ACCESS_DENIED. False when the
        // system or file system has no such rename (then MoveFileExW is tried)
        /// The POSIX-semantics rename; false with a_error the Windows error (MoveFileExW is tried next, and the message names both)
        [[nodiscard]] bool PosixRename(const std::filesystem::path& a_temp, const std::filesystem::path& a_target, DWORD& a_error)
        {
            const HANDLE file = ::CreateFileW(a_temp.c_str(), DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                a_error = ::GetLastError();
                return false;
            }
            const std::wstring name = a_target.wstring();
            std::vector<std::uint8_t> buffer(offsetof(RenameInfoEx, fileName) + (name.size() + 1) * sizeof(WCHAR), 0);
            auto* info = reinterpret_cast<RenameInfoEx*>(buffer.data());
            info->flags = kRenameReplaceIfExists | kRenamePosixSemantics;
            info->rootDirectory = nullptr;
            info->fileNameLength = static_cast<DWORD>(name.size() * sizeof(WCHAR));
            std::memcpy(info->fileName, name.data(), name.size() * sizeof(WCHAR));
            const bool renamed = ::SetFileInformationByHandle(file, kFileRenameInfoEx, info, static_cast<DWORD>(buffer.size())) != FALSE;
            a_error = renamed ? ERROR_SUCCESS : ::GetLastError();
            ::CloseHandle(file);
            return renamed;
        }

        [[nodiscard]] bool WriteAll(HANDLE a_file, const std::string& a_text)
        {
            DWORD written = 0;
            return a_text.size() <= MAXDWORD &&
                   ::WriteFile(a_file, a_text.data(), static_cast<DWORD>(a_text.size()), &written, nullptr) != FALSE &&
                   written == a_text.size();
        }
    }

    std::optional<FileDigest> DigestFile(const std::filesystem::path& a_file, std::string& a_why)
    {
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            a_why = fmt::format("{} does not open", PathText(a_file));
            return std::nullopt;
        }
        FileDigest digest;
        digest.fnv64 = CC::kFnvOffset;
        std::vector<char> piece(kReadPiece);
        while (in) {
            in.read(piece.data(), static_cast<std::streamsize>(piece.size()));
            const auto got = static_cast<std::size_t>(in.gcount());
            digest.fnv64 = CC::Fnv64(std::string_view(piece.data(), got), digest.fnv64);
            digest.bytes += got;
        }
        if (in.bad()) {
            a_why = fmt::format("reading {} failed", PathText(a_file));
            return std::nullopt;
        }
        return digest;
    }

    std::filesystem::path TempPathOf(const std::filesystem::path& a_path)
    {
        std::filesystem::path temp = a_path;
        temp += kTempFileSuffix;
        return temp;
    }

    void RemoveQuietly(const std::filesystem::path& a_file) noexcept
    {
        std::error_code ec;
        std::filesystem::remove(a_file, ec);
    }

    bool CommitTemp(const std::filesystem::path& a_temp, const std::filesystem::path& a_target, std::string& a_why)
    {
        std::error_code ec;
        std::filesystem::create_directories(a_target.parent_path(), ec);
        if (ec) {
            a_why = fmt::format("making the folder of {} failed: {}", PathText(a_target), ec.message());
            return false;
        }
        DWORD posixError = ERROR_SUCCESS;
        if (PosixRename(a_temp, a_target, posixError)) {
            return true;
        }
        if (!::MoveFileExW(a_temp.c_str(), a_target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            a_why = fmt::format("renaming {} into place failed (POSIX rename: Windows error {}; MoveFileExW: Windows error {})",
                PathText(a_temp), posixError, ::GetLastError());
            return false;
        }
        return true;
    }

    int RemoveTempFiles(const std::filesystem::path& a_staging)
    {
        std::vector<std::filesystem::path> temps{ TempPathOf(a_staging / CC::kProgressFile) };
        for (const auto root : { BlackFlame::DataPathRule::kMeshesRoot, BlackFlame::DataPathRule::kTexturesRoot }) {
            const auto folder = StagedFile(a_staging, std::string(root) + std::string(BlackFlame::DataPathRule::kCopiesFolder));
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                std::error_code typeError;
                if (it->is_regular_file(typeError) && !it->is_symlink(typeError) && it->path().extension() == kTempFileSuffix) {
                    temps.push_back(it->path());
                }
            }
        }
        int removed = 0;
        for (const auto& file : temps) {
            std::error_code removeError;
            removed += std::filesystem::remove(file, removeError) ? 1 : 0;
        }
        return removed;
    }

    std::filesystem::path StagedFile(const std::filesystem::path& a_staging, const std::string& a_relative)
    {
        return a_staging / std::filesystem::path(a_relative);  // the copy paths are ASCII (P0 fix) or the system code page
    }

    ProgressLog::~ProgressLog()
    {
        if (m_handle) {
            ::CloseHandle(m_handle);
        }
    }

    bool ProgressLog::Compact(const std::filesystem::path& a_file, std::string& a_why)
    {
        bool exists = false;
        const auto text = ReadText(a_file, exists);
        if (!text) {
            a_why = fmt::format("{} is there but does not read (left as it is; run again)", PathText(a_file));
            return false;
        }
        const auto parsed = CC::ParseProgress(*text);
        m_compaction = {};
        m_compaction.read = parsed.lines.size();
        m_compaction.badLines = parsed.errors.size();
        m_compaction.cutLastLine = parsed.cutLastLine;
        std::map<std::uint64_t, std::size_t> lastAt;  // key -> its last line
        for (std::size_t i = 0; i < parsed.lines.size(); ++i) {
            lastAt[parsed.lines[i].key] = i;
        }
        std::vector<std::size_t> keep;
        const auto hashStart = std::chrono::steady_clock::now();
        for (const auto& [key, at] : lastAt) {
            const auto& line = parsed.lines[at];
            if (line.outcome == CC::Outcome::kFailed) {
                continue;  // retried at this run; an older ok line of the key does not come back (OCR review 3)
            }
            if (CC::IsPublishable(line.outcome) && !line.path.empty() && !StagedMatches(line.path, line.fnv64, line.bytes)) {
                continue;  // its file is gone or other bytes: rebuilt (the digest is remembered for the reuse check)
            }
            keep.push_back(at);
        }
        m_compaction.hashMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - hashStart).count());
        std::sort(keep.begin(), keep.end());
        std::string out;
        for (const auto at : keep) {
            const auto& line = parsed.lines[at];
            const auto written = CC::WriteProgressLine(line);
            if (!written) {
                continue;  // parsed but would not be written back the same: dropped (the object is rebuilt)
            }
            out += *written;
            m_last[line.key] = line;
            ++m_compaction.kept;
        }
        for (const auto& error : parsed.errors) {
            std::cout << fmt::format("WARN  progress.jsonl line {}: {} (dropped)\n", error.lineNumber, error.error);
        }
        if (!exists && out.empty()) {
            return true;
        }
        const auto temp = TempPathOf(a_file);
        // written, flushed to the disk (the contract: flush, then rename), closed, renamed; the temp removed on a failure
        const HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool written = file != INVALID_HANDLE_VALUE && (out.empty() || WriteAll(file, out)) && ::FlushFileBuffers(file) != FALSE;
        if (file != INVALID_HANDLE_VALUE) {
            ::CloseHandle(file);
        }
        if (!written) {
            a_why = fmt::format("writing {} failed (Windows error {})", PathText(temp), ::GetLastError());
            RemoveQuietly(temp);
            return false;
        }
        if (!CommitTemp(temp, a_file, a_why)) {
            RemoveQuietly(temp);
            return false;
        }
        return true;
    }

    bool ProgressLog::Open(const std::filesystem::path& a_staging, std::string& a_why)
    {
        if (m_handle) {
            ::CloseHandle(m_handle);  // a second Open (not done by the builder) closes the first handle
            m_handle = nullptr;
        }
        m_staging = a_staging;
        m_last.clear();
        m_digests.clear();
        m_broken = false;
        LoadPublished();
        std::error_code ec;
        std::filesystem::create_directories(a_staging, ec);
        if (ec) {
            a_why = fmt::format("the staging folder {} cannot be made: {}", PathText(a_staging), ec.message());
            return false;
        }
        const auto file = a_staging / BlackFlame::CopyContract::kProgressFile;
        if (!Compact(file, a_why)) {
            return false;
        }
        const HANDLE handle = ::CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            a_why = fmt::format("{} does not open for appending (Windows error {})", PathText(file), ::GetLastError());
            return false;
        }
        m_handle = handle;
        return true;
    }

    bool ProgressLog::Append(const CC::ProgressLine& a_line, std::string& a_why)
    {
        // A reason may quote a path in the system code page: its other bytes as '?' so the line stays UTF-8 (Codex review 1)
        CC::ProgressLine line = a_line;
        std::replace_if(line.reason.begin(), line.reason.end(), [](char c) { return static_cast<unsigned char>(c) >= kFirstNonAscii; }, '?');
        const auto text = CC::WriteProgressLine(line);
        if (!text) {
            a_why = "the contract's writer refuses the line (a path or reason that is not UTF-8, or a field it would not read back)";
            return false;
        }
        if (m_broken || !m_handle || !WriteAll(m_handle, *text)) {
            a_why = m_broken ? std::string("an earlier write to progress.jsonl failed") :
                               fmt::format("appending to progress.jsonl failed (Windows error {})", ::GetLastError());
            m_broken = true;
            return false;
        }
        m_last[line.key] = std::move(line);
        return true;
    }

    const CC::ProgressLine* ProgressLog::Last(std::uint64_t a_key) const
    {
        const auto it = m_last.find(a_key);
        return it == m_last.end() ? nullptr : &it->second;
    }

    bool ProgressLog::StagedMatches(const std::string& a_relative, std::uint64_t a_fnv64, std::optional<std::uint64_t> a_bytes)
    {
        auto it = m_digests.find(a_relative);
        if (it == m_digests.end()) {
            std::string why;
            it = m_digests.emplace(a_relative, DigestFile(StagedFile(m_staging, a_relative), why)).first;
        }
        if (it->second && it->second->fnv64 == a_fnv64 && (!a_bytes || it->second->bytes == *a_bytes)) {
            return true;
        }
        // (disk trim) the DLL found these bytes in Data this start and may have deleted the staging duplicate
        return CC::PublishedHolds(m_published, a_relative, a_fnv64, a_bytes);
    }

    void ProgressLog::LoadPublished()
    {
        m_published.clear();
        bool exists = false;
        const auto file = m_staging / CC::kPublishedFile;
        const auto text = ReadText(file, exists);
        if (!exists) {
            return;  // a DLL before the disk trim, or none written this start: staging alone counts
        }
        const auto parsed = text ? CC::ParsePublished(*text) : CC::PublishedParse{};
        if (!parsed.headerOk) {
            std::cout << fmt::format("WARN  {} does not read: staging alone counts\n", PathText(file));
            return;
        }
        if (!m_publishedToken || parsed.token != *m_publishedToken) {
            // review Codex: a list an earlier start left behind (its removal failed) never vouches for this start's Data
            std::cout << fmt::format("WARN  {} is not this start's list (token {}, the request's {}): staging alone counts\n",
                PathText(file), CC::Hex16(parsed.token), m_publishedToken ? CC::Hex16(*m_publishedToken) : std::string("none"));
            return;
        }
        m_published = parsed.list;
        std::cout << fmt::format("published {} file(s) in Data listed by the DLL{}\n", m_published.size(),
            parsed.refused == 0 ? std::string() : fmt::format(", {} line(s) refused", parsed.refused));
    }

    void ProgressLog::NoteWritten(const std::string& a_relative, const FileDigest& a_digest)
    {
        m_digests[a_relative] = a_digest;
    }

    bool ProgressLog::Reusable(std::uint64_t a_key, std::uint64_t a_cap)
    {
        const auto* line = Last(a_key);
        if (!line) {
            return false;
        }
        if (line->outcome == CC::Outcome::kTooLarge) {
            return line->cap >= a_cap;  // a larger cap tries again (the cap is no part of the key)
        }
        if (CC::IsTerminal(line->outcome)) {
            return true;
        }
        if (!CC::IsPublishable(line->outcome)) {
            return false;
        }
        if (!line->path.empty() && !StagedMatches(line->path, line->fnv64, line->bytes)) {
            return false;
        }
        return std::all_of(line->deps.begin(), line->deps.end(),
            [&](const CC::ProgressDep& a_dep) { return a_dep.isAddn || StagedMatches(a_dep.path, a_dep.fnv64); });
    }
}
