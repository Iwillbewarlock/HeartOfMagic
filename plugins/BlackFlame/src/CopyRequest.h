#pragma once

#include "Common.h"

#include "FireClosure.h"

#include <filesystem>
#include <optional>
#include <string>

// =============================================================================
// COPY REQUEST (v2 expansion C1) - what the builder needs, written at kDataLoaded
// =============================================================================
// <SKSE log folder>\HeartOfMagic_BlackFlame\ (SKSE::log::log_directory(): outside the VFS, so MO2's
// overwrite stays clean, and the builder finds it without MO2):
//   request.json         version 3: the closure's copy-type forms (source, type, model, muzzle, bounds, scalars,
//                        the spell that reached it), what was excluded and why, every ADDN record (ours marked
//                        own), the closure's impact and hazard models, every NIF read (path, size, fnv64, ok)
//   data\meshes\...      the winning bytes of every NIF the request names (form and muzzle models, ADDN models),
//                        rewritten only when the fingerprint differs from dump-manifest.json's
//   dump-manifest.json   the fingerprint the dump was made for
// The fingerprint is one formula, used by the request (the builder copies it into the copies map) and by the
// freshness check at every start: FNV-1a 64 over the sorted lines (type, source, model, muzzle) of every closure
// form, (index, source, model) of every ADDN not in our plugins, and (path, totalSize, fnv64) of every NIF. Scalars,
// EditorIDs and bounds are left out (the DLL fills those from the live forms); so are our own plugins' forms and
// ADDNs (a built copies plugin must not make its own request stale). The NIF bytes are read at every start on the
// kDataLoaded thread; the time is logged.
// Each part - the fingerprint, the request, the dump - has its own try/catch: a failure is logged and kDataLoaded
// goes on to install the hooks.

namespace BlackFlame
{
    struct CopyRequestState
    {
        bool computed = false;        // the fingerprint was computed
        std::string fingerprint;      // "fnv64:<16 hex>"
        bool requestWritten = false;
        std::string dumpState = "not written";  // "written", "unchanged", "not written", or what failed
        std::size_t forms = 0;
        std::size_t excluded = 0;
        std::size_t nifs = 0;
        std::size_t nifsFailed = 0;
        long long readMs = 0;         // NIF reads and hashing
        long long totalMs = 0;
        std::filesystem::path folder; // where the request lives (empty without a log folder)
    };

    /// kDataLoaded thread, after BuildFireClosure: the fingerprint always; the request and the dump when writeRequest is on
    void PrepareCopyRequest(const FireClosure& a_closure);

    [[nodiscard]] const CopyRequestState& GetCopyRequestState() noexcept;

    /// The folder for the request and the dump, or nullopt without an SKSE log folder
    [[nodiscard]] std::optional<std::filesystem::path> RequestFolder();
}
