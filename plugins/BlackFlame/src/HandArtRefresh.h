#pragma once

#include "Common.h"

#include <cstdint>
#include <string>

// =============================================================================
// HAND ART REFRESH - the player's readied hand art follows the buff at the next tick, ~100 ms (AE 1.6.1170 / 1.7.104)
// =============================================================================
// The copy casting art is handed out only where the engine calls SetCastingArt (CopySwapArt.cpp, 38784 +0x91),
// which is when a hand readies a spell; SetCastingArt (34150) also does nothing while an art is attached (it returns
// when ActorMagicCaster flag kCastingArtAttached, 1 << 3, is set: 1.6.1170 code read 2026-10-17). So hands readied
// before the buff kept the original art for the whole buff, and hands readied during it kept the copy after it.
// When the buff flag changes, each drawn hand of the player whose selected spell is a fire spell, and whose
// castingArt is that spell's costliest effect's art or its copy (or unset with an art attached - an idle drawn hand,
// run 7 2026-10-20 - when that effect's art has a copy), gets the art PickCastingArtImpl picks now, by the
// engine's own unready/ready steps (38784): DetachCastingArt (34152, with true, as 38784's unready branch calls it
// whatever the flags), then SetCastingArt (34150). ActorMagicCaster::Update's first call, CheckAttachCastingArt
// (34185), clones and attaches the new art over the next frames. Only from the main-thread queue (MainThreadQueue.h:
// after the frame's job wait), never from a hook or the ticker thread. Tried at the next tick after the change; a hand
// whose caster is not in state kNone (ready, charging or casting; a concentration spell may be held long) waits for up
// to kMaxCastWaitTicks, a hand still
// being drawn or a player without 3D for up to kMaxRetryTicks unpaused ticks; a paused game waits without counting.
// Only on 1.6.1170 / 1.7.104 (Patch::IsCodeReadBuild; 34152 and 38784 were read on 1.6.1170, 1.7.104 is checked only by
// the install-time byte patterns), when the SetCastingArt site is hooked with
// `mov rcx, rbx` before its castingArt load, and the 34152 call in 38784 is found at install (one E8 preceded by
// mov dl,1; mov rcx,rbx: the same caster register). Our call goes to SetCastingArt itself, so a redirect another
// plugin put on the +0x91 call site does not see it. SE: off, as the site.

namespace BlackFlame
{
    /// kDataLoaded, from InstallArtSite after the SetCastingArt site was hooked: a_caller is 38784, a_site the hooked
    /// call in it, a_setCastingArt 34150's address
    void InstallHandArtRefresh(std::uintptr_t a_caller, std::uintptr_t a_site, std::uintptr_t a_setCastingArt);

    /// The buff flag changed (RecomputeBuff): both hands become pending (the next tick tries them)
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
        std::uint64_t refreshed = 0;   // Detach + SetCastingArt calls made
        std::uint64_t wasPending = 0;  // of those, with the art not attached yet (its clone pending)
        std::uint64_t gaveUp = 0;      // a hand not in kNone after kMaxCastWaitTicks, or still being drawn
                                       // or without the player's 3D after kMaxRetryTicks
    };

    [[nodiscard]] HandArtRefreshStatus GetHandArtRefreshStatus();
}
