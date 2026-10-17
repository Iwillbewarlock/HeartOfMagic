#pragma once

#include "Common.h"

#include <cstdint>
#include <string>

// =============================================================================
// HAND ART REFRESH - the player's readied hand art follows the buff at once (AE)
// =============================================================================
// The copy casting art is handed out only where the engine calls SetCastingArt (CopySwapArt.cpp, 38784 +0x91),
// which is when a hand readies a spell; SetCastingArt (34150) also does nothing while an art is attached (it returns
// when ActorMagicCaster flag kCastingArtAttached, 1 << 3, is set: 1.6.1170 code read 2026-10-17). So hands readied
// before the buff kept the original art for the whole buff, and hands readied during it kept the copy after it.
// When the buff flag changes, each drawn hand of the player whose selected spell is a fire spell, and whose
// castingArt is that spell's costliest effect's art or its copy, gets the art PickCastingArtImpl picks now, by the
// engine's own unready/ready steps (38784): DetachCastingArt (34152, with true as 38784's unready branch passes it),
// then SetCastingArt (34150). ActorMagicCaster::Update's first call, CheckAttachCastingArt (34185), clones and
// attaches the new art over the next frames. Only from the main-thread queue (MainThreadQueue.h: after the frame's
// job wait), never from a hook or the ticker thread. A hand that is charging or casting, or a paused game, is retried
// every tick for up to kMaxRetryTicks unpaused ticks. AE only, and only when the SetCastingArt site is hooked and the
// 34152 call in 38784 is found at install (one E8 preceded by mov dl,1; mov rcx,rbx). SE: off, as the site.

namespace BlackFlame
{
    /// kDataLoaded, from InstallArtSite after the SetCastingArt site was hooked: checks the ids and that 38784 (at
    /// a_caller) calls 34152 once with dl = 1; a_setCastingArt is 34150's address
    void InstallHandArtRefresh(std::uintptr_t a_caller, std::uintptr_t a_setCastingArt);

    /// The buff flag changed (RecomputeBuff, main thread): both hands become pending and are tried at once
    void RequestHandArtRefresh(const char* a_reason);

    /// Ticker task (main-thread queue): tries the pending hands
    void TickHandArtRefresh();

    /// Any thread: a hand is pending (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPendingHandArtRefresh() noexcept;

    /// kPreLoadGame / kNewGame: nothing pending
    void ResetHandArtRefresh();

    struct HandArtRefreshStatus
    {
        bool installed = false;
        std::string note;
        std::uint64_t detached = 0;  // Detach + SetCastingArt (the art was attached)
        std::uint64_t setOnly = 0;   // SetCastingArt alone (its clone was still pending)
        std::uint64_t gaveUp = 0;    // a hand still charging, casting or paused after kMaxRetryTicks
    };

    [[nodiscard]] HandArtRefreshStatus GetHandArtRefreshStatus();
}
