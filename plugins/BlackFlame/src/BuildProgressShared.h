#pragma once

// =============================================================================
// BUILD PROGRESS SHARED - the state BuildProgress.cpp's worker and readers share (and BuildProgressTrim.cpp reads)
// =============================================================================
// Split from BuildProgress.cpp (600-line rule); only BuildProgress*.cpp include it.

#include "BuildProgress.h"
#include "DataFiles.h"
#include "ProgressTail.h"
#include "TrackedMutex.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace BlackFlame::BuildProgress::Detail
{
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

    inline Shared& The()
    {
        static auto* shared = std::make_unique<Shared>().release();
        return *shared;
    }
}
