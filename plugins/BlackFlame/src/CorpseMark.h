#pragma once

#include "Common.h"

#include <cstdint>
#include <string>

// =============================================================================
// CORPSE MARK (2026-10-20, design v2: the FEC fire on a Black Flame kill goes grey)
// =============================================================================
// The only state written into the game here is one SRE flag bit; CorpseGrey.h acts on it at attach.
// 1. Mark, in the 19872 -> 34910 thunk (HitPathObserve.cpp) after the ShaderReferenceEffect
//    constructor returned (it zeroes +0x130 at 5CF840; 19872 then tail-jumps 34762, which Inits the
//    SRE and adds it to ProcessLists, so no other thread sees it yet). An OwnedController (Papyrus
//    EffectShader.Play: FEC) whose target is an Actor dying or dead, killed by the player (myKiller's
//    handle value against the player's), whose last elemental hit (fire, frost or shock;
//    HitPathAttribution FireHits, any caster, fed by the effect start and the magic effect apply
//    sink, MagicApplySink.h) is the player's stoked fire at most corpseWindowGameSeconds game seconds old
//    (GameClock.h), or whose window an earlier mark kept open: SRE flags |= 1 << 24 (no engine use
//    found, findings Follow-up 1) and the target's window stays open corpseWindowGameSeconds more
//    (an FEC chain goes on after the last hit). SaveGame 34920 saves +0x130, LoadGame 34921 reads it
//    back. greyCorpseShaders false: the same decision, logged, no bit. The target is resolved for
//    every candidate while "not marked" lines are left, then only while some remembered fire hit is
//    in its window. Lines:
//    "corpse mark [n]: ..." (48 a session), and for other dying or dead targets the player killed
//    "corpse not marked [n]: ... <reason>" (32 a session).
// 2. The swap at attach (hooks A and B of design v2 §3, the run 8 attach probe turned into hook A):
//    CorpseGrey.h. Its "corpse attach [n]" line also says whether a save was loaded with no stoke
//    window and less than 60 s since (IsAfterLoad: the bit after a load, H4).
// AE 1.6.1170 only (HitPath::Install's runtime and id gate).

namespace BlackFlame::HitPath::Detail
{
    inline constexpr std::uint32_t kCorpseGreyBit = 1u << 24;

    /// The ctor thunk after the constructor returned (its thread: the Papyrus VM's): a_effect is the new ShaderReferenceEffect.
    /// Guarded inside: a fault or a std::exception skips this call only (reported once)
    void MarkCorpseEffect(void* a_effect, const RE::ReferenceEffectController* a_controller, std::uintptr_t a_ownedVtable);

    /// kPostLoadGame: "after a load" starts (until the next stoke window starts, or 60 s)
    void NotePostLoad() noexcept;

    /// A stoke window starts (HitPath::ResetWindow): "after a load" ends
    void ClearPostLoad() noexcept;

    /// Since the last kPostLoadGame (no stoke window since, at most 60 s): the attach line's "after a load"
    [[nodiscard]] bool IsAfterLoad() noexcept;

    /// "corpse mark (session): ..." for the hit path stats line
    [[nodiscard]] std::string CorpseStats();
}
