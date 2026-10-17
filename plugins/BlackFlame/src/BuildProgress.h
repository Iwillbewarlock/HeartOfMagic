#pragma once

// =============================================================================
// BUILD PROGRESS - the progress.jsonl tail reader and the staging-to-Data copy worker (CopyContractProgress.h)
// =============================================================================
// The builder (a child of the game, BuilderLauncher.h) writes every finished object into the staging folder
// <SKSE log folder>\HeartOfMagic_BlackFlame\staging\ (it mirrors Data) and then appends its line to staging\progress.jsonl.
// This side (design v3.2 §4, v3.3 #3 and #7):
//   start    ReadAtStart, before the builder is launched: the whole progress.jsonl read to its end (the builder compacts it
//            only at its own start, by write-temp and rename, after this read). A line's object is kInData when its file is
//            already in Data with the line's fnv64 (an earlier session copied it), else kStaging
//   tail     a worker thread follows the file (opened with FILE_SHARE_READ | WRITE | DELETE; reopened from its start when
//            its file id changes - the compaction - and every line read again; a last line without '\n' waits)
//   copy     the same worker copies an ok or fallback line's file staging -> Data (through the VFS, a temporary file and a
//            replacing rename, fnv64 checked on both sides) only while the publish window is open (MenuWatch::PublishOpen);
//            when the window closes it finishes the file in hand and stops, saying so. What finishes later stays in staging
//            and is copied at the next start
// CopyGroups asks Find(key) for each slot object; publication itself is CopyGroups' (main thread).

#include "Common.h"

#include "CopyContract.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

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
    };

    /// kDataLoaded (singleLaunch), before the builder is launched: the staging folder and progress.jsonl read to its end
    void ReadAtStart();

    /// After the launch: the tail and copy worker (a detached thread; it stops copying when the publish window closes)
    void StartWorker();

    /// The newest line for a_key and its state (any thread)
    [[nodiscard]] std::optional<Object> Find(std::uint64_t a_key);

    /// Bumped whenever a line or a state changes (CopyGroups re-plans when it moved)
    [[nodiscard]] std::uint64_t Generation() noexcept;

    /// <SKSE log folder>\HeartOfMagic_BlackFlame\staging, or empty without a log folder
    [[nodiscard]] std::filesystem::path StagingFolder();

    struct Stats
    {
        std::size_t lines = 0;        // distinct keys read
        std::size_t badLines = 0;     // lines the parser refused
        std::size_t inData = 0;
        std::size_t copied = 0;       // files copied into Data this session
        std::size_t copyFailures = 0;
        std::size_t reopens = 0;      // the file id changed (the builder compacted it)
        bool copyingStopped = false;  // the publish window closed
    };

    [[nodiscard]] Stats GetStats();
}
