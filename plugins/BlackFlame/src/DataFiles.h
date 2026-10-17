#pragma once

// =============================================================================
// DATA FILES - a copy file under the Data folder: its size, and its FNV-1a 64 read once a session
// =============================================================================
// The slot route checks copies in Data against what a progress line or the ledger recorded (BuildProgress, ApplyLastPublished,
// RegisterObject). A file is read whole at most once a session for its hash: the result is kept by (path, size, write time)
// and read again only when one of those changed (our own copy into Data calls Forget). Through the Win32 path in-process, so
// through MO2's VFS. Any thread (one lock).

#include "Common.h"

#include <cstdint>
#include <optional>
#include <string>

namespace BlackFlame::DataFiles
{
    struct Hash
    {
        std::uint64_t size = 0;
        std::uint64_t fnv64 = 0;
    };

    /// a_dataPath ("meshes\..." / "textures\...") under the Data folder read whole (or the session's cached result); nullopt
    /// when it does not open
    [[nodiscard]] std::optional<Hash> HashOf(const std::string& a_dataPath);

    /// Its size only (no byte read); nullopt when it is not there
    [[nodiscard]] std::optional<std::uint64_t> SizeOf(const std::string& a_dataPath);

    /// The file holds a_size bytes whose FNV-1a 64 is a_fnv64
    [[nodiscard]] bool Matches(const std::string& a_dataPath, std::uint64_t a_size, std::uint64_t a_fnv64);

    /// a_dataPath was written: its cached hash is dropped
    void Forget(const std::string& a_dataPath);
}
