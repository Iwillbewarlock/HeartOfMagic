#pragma once

// =============================================================================
// COPY MODEL PROBE CACHE - the probe cache's key and its id text (CopyModelProbe.cpp, internal)
// =============================================================================

#include "Common.h"

#include <filesystem>
#include <optional>
#include <string>

namespace BlackFlame::ProbeCache
{
    inline constexpr const char* kCacheName = "HeartOfMagic_BlackFlame_probe.json";

    /// The copies map's identity: its bytes (FNV-1a 64), its last write time (a builder run rewrites it even when the bytes
    /// stay the same, e.g. after a builder fix) and this plugin's version. Empty when the map cannot be read
    [[nodiscard]] std::string MapIdentity();

    /// <the request folder>\HeartOfMagic_BlackFlame_probe.json, or nullopt without one
    [[nodiscard]] std::optional<std::filesystem::path> CachePath();

    /// A copy's local id as the cache writes it (the copies plugin is an ESL: three hex digits)
    [[nodiscard]] std::string LocalHex(RE::FormID a_copy);

    /// The copy a cached local id names now, or 0
    [[nodiscard]] RE::FormID CopyFromLocal(const std::string& a_hex);
}
