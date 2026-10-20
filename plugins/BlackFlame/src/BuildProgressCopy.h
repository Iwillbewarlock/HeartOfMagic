#pragma once

// =============================================================================
// BUILD PROGRESS COPY - one progress line's file staging -> Data (BuildProgress.cpp's worker; split from it for its size)
// =============================================================================
// CopyIn: through the VFS, the staging bytes checked against the line, a temporary file, a replacing rename, the Data file
// read back; a file already at the path makes the copy wait beside it as <path>.next, listed in DeferredList for the next
// start (BuildProgress::ApplyDeferred). Copies, never moves (the builder's compaction and cache need the staging files)

#include "Common.h"

#include "CopyContractProgress.h"

#include <filesystem>
#include <string>

namespace BlackFlame::BuildProgress
{
    inline constexpr const char* kDeferredName = "deferred-copies.txt";  // path, bytes, fnv64 of the copies renamed in next start
    inline constexpr const char* kNextSuffix = ".next";

    /// <SKSE log folder>\HeartOfMagic_BlackFlame\deferred-copies.txt
    [[nodiscard]] std::filesystem::path DeferredList();

    /// a_line's file into Data; false with a_why (a_deferred: put beside another file as .next, listed for the next start)
    [[nodiscard]] bool CopyIn(const CopyContract::ProgressLine& a_line, bool& a_deferred, std::string& a_why);
}
