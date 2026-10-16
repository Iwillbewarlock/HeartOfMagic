#pragma once

#include "Common.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// NIF DUMP (v2 expansion C1) - the winning NIF bytes through the engine's own file system
// =============================================================================
// The engine resolves a model path through its archives and loose files (and MO2's VFS) exactly as
// it will when it loads the model, so the bytes read here are the file the game shows. Read on the
// kDataLoaded thread only (BSResource is not called from another thread).

namespace BlackFlame::NifDump
{
    /// A record's model string as a Data-relative path: ASCII lower case, '/' as '\', repeated '\' folded, a leading
    /// "data\" and "meshes\" dropped and one "meshes\" put in front. nullopt (refused) for an empty path, a drive letter or
    /// ':' anywhere, or a ".." path token: such a path could name a file outside Data and is never read or written.
    [[nodiscard]] std::optional<std::string> NormalizeModelPath(std::string_view a_model);

    /// FNV-1a 64 over a_bytes, continuing from a_hash (start with kFnvOffset)
    inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
    [[nodiscard]] std::uint64_t Fnv64(const void* a_bytes, std::size_t a_size, std::uint64_t a_hash = kFnvOffset) noexcept;

    struct ModelBlob
    {
        std::string dataPath;  // NormalizeModelPath's result
        std::uint64_t size = 0;
        std::uint64_t fnv64 = 0;
        bool ok = false;
        std::string error;  // why not ok
    };

    /// Opens a_dataPath (BSResourceNiBinaryStream), seeks its stream to 0 and reads it 64 KiB at a time with every
    /// return code checked until a read gives 0 bytes; ok only when the bytes read equal the stream's totalSize.
    /// a_outBytes (optional) receives the bytes.
    [[nodiscard]] ModelBlob ReadModel(const std::string& a_dataPath, std::vector<std::byte>* a_outBytes);
}
