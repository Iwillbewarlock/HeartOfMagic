#pragma once

#include "Common.h"

#include "FireClosure.h"

#include <string>

// =============================================================================
// COPY FRESHNESS (v2 expansion C3) - do the built copies still match this load order?
// =============================================================================
// At kDataLoaded, after the copy request (CopyRequest.h), from the copies map and this start's fingerprint:
//   none          no map, or a map with no entry (only the empty ESLs the mod ships)
//   pluginMissing the map was read but HeartOfMagic_BlackFlame_Copies.esp is not loaded, or more than half of its
//                 entries lost their copy form (the output mod is not enabled)
//   notApplied    Y11: the map was there but filling the copies stopped part way (no copy is used this session)
//   texturesMissing AB4: with a matching fingerprint, a decal texture set copy names a grey texture that does not open
//                 (the output mod's textures missing or overwritten, or a loose-file cache out of date)
//   stale         a version 1 map, or the map's request fingerprint differs from this start's (the bytes form when
//                 writeRequest computed it, else the size form), or a decal texture set copy was left out as out of step
//                 with the load order (CopyCounts::textureSetsRejected); with how many closure forms have no copy and how
//                 many map entries lost their source
//   fresh         otherwise
// The player is told once per session (the first save load or new game) through the main-thread queue
// (RE::SendHUDMessage::ShowHUDMessage, the corner notice; without the queue only the log says it); the text is English (this prototype's HUD
// lines are; translation comes with the Heart of Magic panel). Raw ids never go on screen.

namespace BlackFlame
{
    enum class CopyFreshness
    {
        kFresh,
        kStale,
        kNone,
        kPluginMissing,
        kNotApplied,      // Y11: the map was there but filling the copies failed part way (no copy is used this session)
        kTexturesMissing  // AB4: grey decal textures of the copies do not open (the ESL is loaded)
    };

    struct FreshnessState
    {
        CopyFreshness state = CopyFreshness::kNone;
        std::string detail;            // why, for the log, the tester file and the self-test
        std::size_t formsWithoutCopy = 0;
        std::size_t copiesLostSource = 0;
    };

    /// kDataLoaded, after PrepareCopyRequest
    void CheckFreshness(const FireClosure& a_closure);

    /// F10: kDataLoaded could not check (the closure failed); no notice is shown, the log says why
    void MarkFreshnessUnchecked(const std::string& a_why);

    /// Y11: LoadAndFillCopies stopped part way (FormCopies.cpp); CheckFreshness then reports kNotApplied, not "not built"
    void MarkCopiesFillFailed(const std::string& a_why);

    [[nodiscard]] const FreshnessState& GetFreshness() noexcept;

    /// "fresh" / "stale" / "none" / "pluginMissing" / "notApplied" / "texturesMissing"
    [[nodiscard]] const char* FreshnessName(CopyFreshness a_state) noexcept;

    /// The first kPostLoadGame / kNewGame of the session: the HUD line for stale / none / pluginMissing / notApplied, once (nothing when
    /// kDataLoaded never checked; marked shown only once the main-thread queue took the notice)
    void NotifyFreshnessOnce();

    /// The copy model probe found a_count copy NIFs that exist (the plain stream opens them) but that the game's loose-file
    /// index does not know (a cache mod such as CRDW AE out of date). Logged and kept for the tester file; the HUD line is
    /// shown now when the player is in the world, else at the first kPostLoadGame / kNewGame (NotifyFileIndexOnce); W13:
    /// only when CRDW.dll is loaded (the one index layer the line names a fix for), otherwise logged only. Main thread only
    /// (it reads the player's cell; the probe's Finish calls it from the main-thread queue).
    void ReportStaleFileIndex(std::size_t a_count);

    /// The HUD line of ReportStaleFileIndex, once per session (nothing when nothing was reported)
    void NotifyFileIndexOnce();

    /// "" or the stale file index line (the tester file)
    [[nodiscard]] std::string StaleFileIndexText();

    /// W20: the loose-file index state the copy model probe's cache key carries: CRDW.dll's size and write time (or that
    /// it is not loaded) and an FNV-1a of each copy NIF's path, size and write time under Data\meshes. kDataLoaded thread.
    [[nodiscard]] std::string FileIndexState(const std::vector<std::string>& a_copyModelPaths);
}
