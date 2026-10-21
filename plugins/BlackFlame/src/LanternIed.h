#pragma once

#include "Common.h"

// =============================================================================
// LANTERN IED - the worn lantern shown by Immersive Equipment Displays when it is installed (README "Lantern on IED";
// IED 1.7.4, SlavicPotato, its Papyrus API as IED.psc declares it)
// =============================================================================
// kDataLoaded: IED counts as there when ImmersiveEquipmentDisplays.dll is loaded, the VM knows the script type IED and
// IED.GetVersion() can be sent. With IED and lanternIed, ARMA 0x80E's biped models (and its first-person ones when set)
// become lantern_empty.nif (a root node only), so the worn ARMO only holds slot 60 and IED's copy at the hip is what
// shows; without IED, or with lanternIed false, the skinned lantern_worn.nif stays. At each game load (kPostLoadGame, a
// load that succeeded) and new game, one IED entry "Lantern" under this plugin's key is set up for the player:
// CreateItemActor once (male), then both sexes disabled, then for each sex the form (ARMO 0x80D), inventory form, node NPC
// Pelvis [Pelv], parent attachment (as the Lights On profiles), world model, Havok off, position, rotation (degrees) and
// scale (LanternIedRules.h, Config.h), then both enabled as the player wears the lantern now, then the check (ItemExists,
// ItemEnabled per sex: a PASS/FAIL line each). A TESEquipEvent of the player and the lantern queues the enabled state of
// both sexes and the check. With IED and lanternIed false the entry is deleted at each load instead (a save that had it
// loses it). Every call is a DispatchStaticCall on the main thread (the queue, MainThreadQueue.h); the calls run one after
// another, each sent from the task its predecessor's result queued, each result logged; a call whose result has not come
// back in 5 s is given up on the ticker (Tick, an unknown result, not a false) and a late result is ignored. If the entry
// cannot be made (CreateItemActor and SetItemFormActor male both returned false, ItemExistsActor returned false after the
// setup, or GetVersion gives no int) the ARMA's models are put back, no entry is set up for the session and the entry is
// deleted at each load instead. Only the player is registered: a follower wearing the lantern shows nothing while IED is on (the ARMA is
// empty).

namespace BlackFlame::LanternIed
{
    /// kDataLoaded, after LoadForms: detection, IED.GetVersion, the ARMA swap and its check, the equip sink
    void Init();

    /// kPostLoadGame (a_loaded: the load's success flag) and kNewGame (true): the entry set up, or deleted with lanternIed false
    void OnGameReady(const char* a_why, bool a_loaded);

    /// Ticker task (main thread): a call waiting longer than its result wait is given up on
    void Tick();

    /// Any thread: a call is out or waiting (the ticker then stays at 100 ms)
    [[nodiscard]] bool HasPending() noexcept;
}
