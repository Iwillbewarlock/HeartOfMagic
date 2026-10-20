#pragma once

// =============================================================================
// BUILD PROGRESS - the progress.jsonl tail reader and the staging-to-Data copy worker (CopyContractProgress.h)
// =============================================================================
// The builder (a child of the game, BuilderLauncher.h) writes every finished object into the staging folder
// <SKSE log folder>\HeartOfMagic_BlackFlame\staging\ (it mirrors Data) and then appends its line to staging\progress.jsonl.
// This side (design v3.2 §4, v3.3 #3 and #7):
//   start    ReadAtStart, before the builder is launched: the whole progress.jsonl read to its end (the builder compacts it
//            only at its own start, by write-temp and rename, after this read). A line's object is kInData when its file is
//            already in Data with the line's fnv64 (an earlier session copied it), else kStaging. Nothing is copied on the
//            kDataLoaded thread
//   tail     a worker thread follows the file (opened with FILE_SHARE_READ | WRITE | DELETE; reopened from its start when
//            its file id changes - the compaction - and every line read again; a last line without '\n' is read again from its
//            start at the next poll, since the builder drops a cut last line before it appends). The last line of a key wins
//   copy     the same worker copies an ok or fallback line's file staging -> Data (through the VFS, a temporary file and a
//            replacing rename, fnv64 checked on both sides) only while the publish window is open (MenuWatch::PublishOpen);
//            when the window closes it finishes the file in hand and stops, saying so. What finishes later stays in staging
//            and is copied at the next start
//   keep     review round 1 D1: a line whose path holds another file that a published ledger object stands on (another
//            build of the same key: GPU bytes differ) keeps that file, never deferred, and Find names it by the Data file
// CopyGroups asks Find(key) for each slot object; publication itself is CopyGroups' (main thread).

#include "Common.h"

#include "CopyContractProgress.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BlackFlame::BuildProgress
{
    namespace CC = CopyContract;

    enum class State : std::uint8_t
    {
        kStaging,   // an ok or fallback line; its file (or a copy dep) is not in Data yet
        kInData,    // its file and every copy dep are in Data, fnv64 checked
        kFailed,    // retryable: the next run tries again
        kTerminal,  // too_large or not_dumped: the member leaves its group
    };

    struct Object
    {
        State state = State::kStaging;
        CC::ProgressLine line;
        bool copiedThisSession = false;   // its file entered Data this session (not in the startup index: CopyGroups needs FileIndex)
        bool replacedThisSession = false; // and replaced another file at that path (the index may hold the old size: next start)
    };

    /// kDataLoaded (singleLaunch), before the builder is launched: progress.jsonl read to its end and each object's Data
    /// state checked (sizes first, a hash once a session); no file is copied on this thread (the worker does, after W1).
    /// A line whose file is neither in Data nor in staging (the old-copies cleanup removed it) is dropped, counted in the log
    void ReadAtStart();

    /// kDataLoaded, before anything uses a copy path (SingleLaunch, before ApplyLastPublished): the copies an earlier session
    /// could not put in place (another file was at the path) renamed in from <path>.next; they count as copied and replaced.
    /// (reviewer round 2) A .next whose Data file a published ledger object stands on is deleted instead (the header's "keep")
    void ApplyDeferred();

    /// a_path with a_fnv64 entered Data this session (copied by the worker, or renamed in by ApplyDeferred): the startup
    /// index does not know it, so it is used only through FileIndex (any thread)
    [[nodiscard]] bool CopiedNow(const std::string& a_path, std::uint64_t a_fnv64);

    /// a_path with a_fnv64 replaced another file at its path this session (ApplyDeferred): the index may hold the old
    /// file's size, so nothing standing on it is published before the next start (any thread)
    [[nodiscard]] bool ReplacedNow(const std::string& a_path, std::uint64_t a_fnv64);

    /// Any of a_files (SlotLedgerModel::FilesOf) replaced this session
    [[nodiscard]] inline bool AnyReplacedNow(const std::vector<std::pair<std::string, std::uint64_t>>& a_files)
    {
        for (const auto& [path, fnv] : a_files) {
            if (ReplacedNow(path, fnv)) {
                return true;
            }
        }
        return false;
    }

    /// After the launch: the tail and copy worker (a detached thread; it stops copying when the publish window closes)
    void StartWorker();

    /// kDataLoaded, in place of StartWorker when another game process holds the request folder (review round 1 D2): nothing
    /// more is read or copied this session, so the start's read counts as caught up
    void SkipWorker();

    /// The newest line for a_key and its state (any thread); a kept file (the header's "keep") by the Data file's size and
    /// fnv64, as the object's own file and as a dep
    [[nodiscard]] std::optional<Object> Find(std::uint64_t a_key);

    /// Bumped whenever a line or a state changes (CopyGroups re-plans when it moved)
    [[nodiscard]] std::uint64_t Generation() noexcept;

    /// The builder is not running (ended, or never started) and the worker read and settled everything after that: a key
    /// with no line now will not get one this session
    [[nodiscard]] bool CaughtUp() noexcept;

    /// <SKSE log folder>\HeartOfMagic_BlackFlame\staging, or empty without a log folder
    [[nodiscard]] std::filesystem::path StagingFolder();

    struct Stats
    {
        std::size_t lines = 0;        // distinct keys read
        std::size_t badLines = 0;     // lines the parser refused
        std::size_t inData = 0;
        std::size_t copied = 0;       // files copied into Data this session
        std::size_t copyFailures = 0;
        std::size_t kept = 0;         // published copies kept for another build of their key (review round 1 D1)
        std::size_t reopens = 0;      // the file id changed (the builder compacted it)
        bool copyingStopped = false;  // the publish window closed
    };

    [[nodiscard]] Stats GetStats();
}
