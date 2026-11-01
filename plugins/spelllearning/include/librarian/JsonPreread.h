#pragma once

#include <filesystem>
#include <vector>

#include <nlohmann/json.hpp>

// =============================================================================
// JsonPreread - JSON files read before data load, taken at data load
//
// The catalog and the perk adapter files are read and parsed on a worker at
// kPostLoad (files only, no game forms), so the data loaded handler does not
// wait on them (specs/003-load-time/plan.md, Phase 6c). A file taken must
// still have the size and write time the worker saw; otherwise, or when it
// was never read ahead, the caller reads it as before.
// =============================================================================

namespace Librarian::Detail::Preread
{
    // Starts the worker; once a session (a second call does nothing).
    void Start(std::vector<std::filesystem::path> paths);

    // The worker's parse of `path` (waits for the worker if it is still
    // reading), once: true and `document` filled when the file is unchanged
    // since. False when it was not read ahead, did not parse, or changed.
    [[nodiscard]] bool Take(const std::filesystem::path& path, nlohmann::json& document);
}
