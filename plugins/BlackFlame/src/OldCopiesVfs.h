#pragma once

// =============================================================================
// OLD COPIES VFS - the old-copies cleanup's guard on Data under MO2's VFS (OldCopies.h "remove")
// =============================================================================
// review round 1 (reviewer 2): under MO2 (usvfs_x64.dll loaded) a Data file is removed only when its real path
// (OldCopiesFiles::FinalPath) is under MO2's overwrite folder, found from the real path of a file this session's CopyIn wrote
// into Data (a new file the VFS put there: BuildProgress::Find's copiedThisSession and not replacedThisSession). Without such a
// file, or when GetFinalPathNameByHandleW gives the virtual path back (the root would be Data itself), Data is counted only.
// Each decision is logged once: the root and its sample, and the first Data candidate's real path. Worker thread.

#include "Common.h"

#include "OldCopiesFiles.h"
#include "OldCopiesRule.h"

#include <filesystem>
#include <optional>
#include <string>

namespace BlackFlame::OldCopies::Vfs
{
    struct Guard
    {
        bool vfs = false;
        std::optional<std::filesystem::path> overwrite;
        std::string why;  // vfs without overwrite: why
        bool sampleLogged = false;
    };

    /// The guard for a_data (lexically normal): vfs false without usvfs (Data removed as staging is)
    [[nodiscard]] Guard MakeGuard(const std::filesystem::path& a_data, const OldCopiesRule::KeySet& a_keep);

    /// a_guard.overwrite set: every file of a_listing and a_folder itself resolve under it
    [[nodiscard]] bool InOverwrite(Guard& a_guard, const std::filesystem::path& a_folder, const OldCopiesFiles::Listing& a_listing);
}
