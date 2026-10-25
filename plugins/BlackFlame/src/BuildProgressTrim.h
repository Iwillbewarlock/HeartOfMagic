#pragma once

// =============================================================================
// BUILD PROGRESS TRIM - staging\published.tsv and the staging duplicates deleted (disk trim, 2026-10-26)
// =============================================================================
// DiskTrimRules.h holds the rule; this side does the files:
//   list  WritePublished, kDataLoaded after ReadAtStart and before the request and the builder launch (not when another game
//         holds the request folder): the old list removed first, then every progress line's own file and copy dep whose
//         Data file has exactly its size and fnv64 (DataFiles: the session's hash, or the last session's for the same size
//         and write time) written whole as staging\published.tsv. A line whose path holds another build of its key (the
//         "keep" case, BuildProgress.h) does not match, so it is not listed and its staging file stays
//   trim  StartTrim, after the builder launch: a below-normal detached thread deletes the staging file of every listed
//         path (DiskTrimRules::MayTrim: not one of our copies a source names) after it hashed the Data file again without
//         the cache and found the listed bytes; then the copies\<key16>\ folders left empty. It stops when a load begins
//         (the rest at the next start). Files copied into Data this session are listed, and trimmed, at the next start
// A trimmed file that later leaves Data (the Overwrite cleared, another MO2 profile without it) is not listed then: the DLL
// drops its line (ReadAtStart) and the builder builds the object again.

#include "Common.h"

#include "CopyContractProgress.h"
#include "CopyContractPublished.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>

namespace BlackFlame::BuildProgress
{
    /// See the header; false when the list could not be written (no file then: the builder counts staging alone)
    bool WritePublished();

    /// The list WritePublished wrote this session (empty before it, or when it failed)
    [[nodiscard]] CopyContract::PublishedList Published();

    /// Its token, for request.json (CopyContract kPublishedToken); nullopt when no list was written this session
    [[nodiscard]] std::optional<std::uint64_t> PublishedToken();

    /// A key's last progress line as the builder wrote it (no kept file put in its place, unlike Find)
    [[nodiscard]] std::optional<CopyContract::ProgressLine> LastLine(std::uint64_t a_key);

    /// A staging file's size and fnv64 read now, nullopt when it is not there or does not read. Asked only for a line's file
    /// the list lacks (not yet in Data): after the trim that is little
    [[nodiscard]] std::optional<CopyContract::PublishedFile> StagedHash(const std::string& a_path);

    /// See the header (a_keep: our copies a source names, SlotKeys::OwnCopiesNamed)
    void StartTrim(std::set<std::string> a_keep);
}
