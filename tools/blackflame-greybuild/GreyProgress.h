#pragma once

// =============================================================================
// GreyProgress - the key mode's progress.jsonl in the staging folder, and the staging folder's file rules
// =============================================================================
// The line format, writer and parser are the contract's (plugins/BlackFlame/src/CopyContractProgress.h); this is the
// builder's side of the file:
//   - Open (at the builder's start only, v3.3 #3: the DLL has read the file to its end before it launched the builder):
//     every line read, then compacted - for each key its last line, dropped when it is "failed" (no older line comes
//     back), an ok or fallback line only while its file is in staging with its size and fnv64 - written to progress.jsonl.tmp and renamed over progress.jsonl (the
//     DLL's tail reader reopens on the new file id). A cut last line and bad lines are dropped.
//   - Append: one line per finished object in one WriteFile on a handle opened for appending (shared read, write and
//     delete), so another process sees whole lines; the object's file was renamed into place before.
//   - Reusable: an object the staging folder already holds - its key's last line is ok or fallback with its file there
//     with the line's bytes and fnv64, and every path dep there with its fnv64 too; or the line is terminal (too_large
//     only while its cap field is not below this run's cap).
// Copy files are written as <path>.tmp beside their place and renamed (CommitTemp); RemoveTempFiles clears what a run
// that was killed left.

#include "../../plugins/BlackFlame/src/CopyContract.h"
#include "../../plugins/BlackFlame/src/CopyContractProgress.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace GreyBuild
{
    inline constexpr const char* kTempFileSuffix = ".tmp";

    struct FileDigest
    {
        std::uint64_t bytes = 0;
        std::uint64_t fnv64 = 0;
    };

    /// A file's size and FNV-1a 64, read in pieces; nullopt with a_why when it does not open or read
    [[nodiscard]] std::optional<FileDigest> DigestFile(const std::filesystem::path& a_file, std::string& a_why);

    /// a_path with kTempFileSuffix
    [[nodiscard]] std::filesystem::path TempPathOf(const std::filesystem::path& a_path);

    /// a_file removed, an error ignored (a temp file that failed; RemoveTempFiles clears what is left)
    void RemoveQuietly(const std::filesystem::path& a_file) noexcept;

    /// a_temp renamed over a_target (folders made); false with a_why
    [[nodiscard]] bool CommitTemp(const std::filesystem::path& a_temp, const std::filesystem::path& a_target, std::string& a_why);

    /// Every *.tmp the builder can have left in a_staging removed - under its two copies folders
    /// (meshes|textures\heartofmagic\blackflame\copies\) and progress.jsonl.tmp, nothing else; how many
    int RemoveTempFiles(const std::filesystem::path& a_staging);

    /// A staging-relative (= Data-relative) path as a file under a_staging
    [[nodiscard]] std::filesystem::path StagedFile(const std::filesystem::path& a_staging, const std::string& a_relative);

    class ProgressLog
    {
    public:
        struct Compaction
        {
            std::size_t read = 0;       // lines that parsed
            std::size_t kept = 0;       // lines written back
            std::size_t badLines = 0;   // lines that did not parse
            bool cutLastLine = false;   // the file ended inside a line
        };

        ProgressLog() = default;
        ~ProgressLog();
        ProgressLog(const ProgressLog&) = delete;
        ProgressLog& operator=(const ProgressLog&) = delete;

        /// a_staging\progress.jsonl compacted and opened for appending (the folder made); false with a_why
        [[nodiscard]] bool Open(const std::filesystem::path& a_staging, std::string& a_why);

        /// One line appended (the contract's writer); false with a_why when it is refused or not written. A write that fails
        /// latches Broken(): no later line is written on that handle (a partial write would fuse with the next line)
        [[nodiscard]] bool Append(const BlackFlame::CopyContract::ProgressLine& a_line, std::string& a_why);

        /// A write failed (I/O): the run stops; a refusal by the contract's writer does not latch
        [[nodiscard]] bool Broken() const noexcept { return m_broken; }

        /// The key's last line, or null
        [[nodiscard]] const BlackFlame::CopyContract::ProgressLine* Last(std::uint64_t a_key) const;

        /// See the header comment; a_cap is this run's job memory cap
        [[nodiscard]] bool Reusable(std::uint64_t a_key, std::uint64_t a_cap);

        /// a_relative in staging matches a_fnv64 (and a_bytes when given); digests are remembered for the run
        [[nodiscard]] bool StagedMatches(const std::string& a_relative, std::uint64_t a_fnv64, std::optional<std::uint64_t> a_bytes = {});

        /// a_relative was just written with a_digest (the remembered digest replaced)
        void NoteWritten(const std::string& a_relative, const FileDigest& a_digest);

        [[nodiscard]] const Compaction& LastCompaction() const noexcept { return m_compaction; }
        [[nodiscard]] const std::filesystem::path& Staging() const noexcept { return m_staging; }

    private:
        [[nodiscard]] bool Compact(const std::filesystem::path& a_file, std::string& a_why);

        std::filesystem::path m_staging;
        void* m_handle = nullptr;  // HANDLE, closed by the destructor and by a second Open (INVALID_HANDLE_VALUE kept as null)
        bool m_broken = false;
        std::map<std::uint64_t, BlackFlame::CopyContract::ProgressLine> m_last;
        std::map<std::string, std::optional<FileDigest>> m_digests;
        Compaction m_compaction;
    };
}
