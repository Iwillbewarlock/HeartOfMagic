#pragma once

// =============================================================================
// SINGLE LAUNCH - the kDataLoaded order of the slot route (config singleLaunch; README "Single launch: the DLL side")
// =============================================================================
// Main.cpp runs, with singleLaunch on: MenuWatch::Install (always), FileIndex::Init, SlotLedger::Load, the fire sets and the
// fire closure, then OnDataLoaded:
//   1. SlotKeys::Read          the closure's NIFs, the textures they name, the builder exe, the AddOnNode indices
//   2. SlotAssign::AssignAndFill   wants, the ledger's assignment, every slot filled, gates closed, keys
//   3. ApplyLastPublished      W1: last published generations that are still intact set as the slots' models
// and the copies map route (LoadAndFillCopies, the freshness check, the copy model probe) does not run.

#include "Common.h"

#include "FireClosure.h"
#include "SlotKeys.h"

namespace BlackFlame::SingleLaunch
{
    /// kDataLoaded thread, after BuildFireSets and BuildFireClosure
    void OnDataLoaded(const FireClosure& a_closure);

    /// The key inputs read at kDataLoaded (kept for the session: the W2 verification and request v5 read them)
    [[nodiscard]] const SlotKeys::Inputs& Inputs();
}
