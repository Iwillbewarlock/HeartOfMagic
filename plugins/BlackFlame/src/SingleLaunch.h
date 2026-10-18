#pragma once

// =============================================================================
// SINGLE LAUNCH - the kDataLoaded order of the slot route (config singleLaunch; README "Single launch: the DLL side")
// =============================================================================
// Main.cpp runs, with singleLaunch on: MenuWatch::Install (always), FileIndex::Init, SlotLedger::Load, the fire sets and the
// fire closure, then OnDataLoaded:
//   1. SlotKeys::Read          the closure's NIFs, the textures they name, the builder exe, the AddOnNode indices
//   2. SlotAssign::AssignAndFill   wants, the ledger's assignment, every slot filled, gates closed, keys
//      BuilderLauncher::ClaimFolder  the request folder's mutex (review round 1 D2); another game holds it: no
//                              ApplyDeferred, no request or dump, no builder, and BuildProgress::SkipWorker in place of 7
//      BuildProgress::ApplyDeferred  the copies an earlier session left as <path>.next renamed in
//   3. ApplyLastPublished      W1: last published generations that are still intact set as the slots' models
//   4. BuildProgress::ReadAtStart  progress.jsonl to its end (before the builder starts and compacts it)
//   5. CopyGroups::Start       W1: groups ready from the ledger open; the rest verified and opened in the drains
//   6. BuilderLauncher::Start  the builder as a child (launchBuilder, a key prefix, request.json there)
//   7. BuildProgress::StartWorker  the tail and the staging -> Data copy while the publish window is open
// with request v5 and the dump (CopyRequestV5.h) written between 3 and 4, and the progress log line from the main menu on.
// and the copies map route (LoadAndFillCopies, the freshness check, the copy model probe) does not run.

#include "Common.h"

#include "CopyRequestV5.h"
#include "FireClosure.h"
#include "SlotKeys.h"

namespace BlackFlame::SingleLaunch
{
    /// kDataLoaded thread, after BuildFireSets and BuildFireClosure
    void OnDataLoaded(const FireClosure& a_closure);

    /// The key inputs read at kDataLoaded (kept for the session; the NIF bytes are dropped after the dump)
    [[nodiscard]] const SlotKeys::Inputs& Inputs();

    /// The progress log line (main thread; nothing on screen: the player is never asked to wait, bf-no-wait-proposal.md
    /// section 5): "progress: N of M grey copies ready" when the main menu is open and the count moved (at most every 10 s),
    /// "all M grey copies are ready" once; nothing before the main menu or after the publish window closed
    void NotifyProgress();

    /// The dev auto-load may go: publication settled (nothing more can open this session: every group open, failed or
    /// left once the builder ended and its lines are read, or the window closed) or every slot open
    [[nodiscard]] bool ReadyForLoad();

    /// What request v5's write did this session (T0-slots; written false off the route, and when another game holds the
    /// request folder: nothing is written then)
    [[nodiscard]] CopyRequestV5::Result Request();

    /// (reviewer round 2) Another game process held the request folder at kDataLoaded (BuilderLauncher::ClaimFolder): no
    /// request, dump, builder or copy worker this session (T0-slots says so instead of judging the request)
    [[nodiscard]] bool OtherGameHoldsFolder();
}
