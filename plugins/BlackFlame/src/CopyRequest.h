#pragma once

#include "Common.h"

#include "FireClosure.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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
//   data\textures\...    T1: every texture those NIFs name inline (NifDump::TexturePathsIn), read through the same file
//                        system within a per-texture and a total cap (CopyRequestData.h); request.json "textures" lists
//                        each with ok / error. Opened for their size at every start with writeRequest on, read only when
//                        the dump is written. Not part of the fingerprint (a texture-only change does not make the
//                        copies stale); its own size fingerprint in the manifest decides when the dump is written again
//                        Also the TX00 / TX03 textures of the closure's decal texture sets (TXST forms: "textures",
//                        "hasDecal"; the request says "decalTextureSets": true) and our two fixed palettes ("own": read
//                        whole at every start for their hash, so the builder's risk check judges the game's own)
//   dump-manifest.json   the fingerprints the dump was made for (request, textures); the dump code is CopyRequestDump.cpp
// The fingerprint is one formula, used by the request (the builder copies it into the copies map) and by the
// freshness check at every start: FNV-1a 64 over the sorted lines (type, source, model, muzzle) of every closure
// form, (index, source, model) of every ADDN not in our plugins, and (path, totalSize, fnv64) of every NIF. Scalars,
// EditorIDs and bounds are left out (the DLL fills those from the live forms); so are our own plugins' forms and
// ADDNs (a built copies plugin must not make its own request stale). The NIF bytes are read at every start on the
// kDataLoaded thread; the time is logged. With writeRequest false (the players' default) nothing is read.
// Each part - the fingerprint, the request, the dump - has its own try/catch: a failure is logged and kDataLoaded
// goes on to install the hooks.

namespace BlackFlame
{
    struct CopyRequestState
    {
        bool computed = false;        // the fingerprint was computed
        std::string fingerprint;      // "fnv64:<16 hex>"
        std::string sizeFingerprint;  // the same without the NIF byte hashes (path, size): C3's cheap check
        bool requestWritten = false;
        std::string dumpState = "not written";  // "written", "unchanged", "not written", or what failed
        std::size_t forms = 0;
        std::size_t excluded = 0;
        std::size_t nifs = 0;
        std::size_t nifsFailed = 0;
        std::size_t nifsFailedForms = 0;   // of those, a closure form's or a muzzle's model (T0-request FAILs on them)
        std::size_t nifsAbsentForms = 0;   // H1: of nifsFailed, a non-base-game form's model that does not open at all (INFO)
        std::vector<std::string> absentFormSample;  // the first few such paths (UTF-8)
        std::size_t nifsFailedAddons = 0;  // of those, only an ADDN record's model (INFO: often another mod's broken mesh)
        std::vector<std::string> failedAddonSample;  // the first few such ADDN paths (UTF-8)
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
