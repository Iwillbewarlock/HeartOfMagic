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
//   stale         a version 1 map, or the map's request fingerprint differs from this start's (the bytes form when
//                 writeRequest computed it, else the size form); with how many closure forms have no copy and how
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
        kPluginMissing
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

    [[nodiscard]] const FreshnessState& GetFreshness() noexcept;

    /// "fresh" / "stale" / "none" / "pluginMissing"
    [[nodiscard]] const char* FreshnessName(CopyFreshness a_state) noexcept;

    /// The first kPostLoadGame / kNewGame of the session: the HUD line for stale / none / pluginMissing, once (nothing when
    /// kDataLoaded never checked; marked shown only once the main-thread queue took the notice)
    void NotifyFreshnessOnce();
}
