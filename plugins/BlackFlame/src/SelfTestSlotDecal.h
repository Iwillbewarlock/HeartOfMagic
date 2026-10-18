#pragma once

// =============================================================================
// SELF-TEST SLOT DECAL - the decal texture set slots judged by data (single launch; T0-decal on the slot route, E3-slots)
// =============================================================================
// Slot route (SlotAssign::Active), with selfTestSlots:
//   T0-decal  every open PROJ / IPCT slot's decal link is LinkFor of its source's link (an open TXST slot, or the source's
//             set while that slot is closed) and never a closed TXST slot; every open TXST slot holds the strings
//             SlotTxst::Write gave it, each TX00 / TX03 it changed is a texture copy path (textures\...\copies\<key16>\copy.dds),
//             each textureFileID is SlotTxst::ExpectedId's, and its DODT is there exactly when the source's is. With slotTxst:
//             at least one TXST slot open when any is usable (FAIL otherwise, the plan's reasons are in the log)
//   E3-slots  the Fire Rune's own chain (MGEF projectile, PROJ decal and explosion, the explosion's impact set's IPCTs, their
//             decal sets and hazard): the PROJ copy is an open slot; each link of each open copy is LinkFor of the source's;
//             with slotTxst, a decal set of the chain is an open TXST slot with grey copies; the source forms hold no slot
//             (the NPC's and every unswapped path keep the original)

#include "Common.h"

#include <string>

namespace BlackFlame::SelfTest
{
    /// T0-decal on the slot route (StepDecalCover calls it when SlotAssign::Active)
    bool StepDecalSlots();

    /// E3-slots: the rune's chain by data (called once when E3 starts, before the launch)
    void JudgeRuneSlots(const RE::SpellItem* a_rune);
}
