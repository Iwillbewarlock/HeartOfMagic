# Equip ability visuals: refresh and shared ownership

2026-10-23. Confirmed omission: the 19:51 run's original SKSE log, line 715, reports
`equip ability refresh 00080E34 (buff on): not in the actor's spell list: left as is`.
The earlier art getter kept original 00080E32 before the buff (670-672). The active ability exists
without HasSpell, so RemoveSpell/AddSpell cannot refresh it safely. This is independent of whether
the reported red body/floor pixels come from that art; see [aura-light.md](aura-light.md).

## Engine evidence (AE 1.6.1170 only)

The flat image, SHA256 and disassembler are the same inputs as [corpse-engine.md](corpse-engine.md).
Read-only disassembly commands with that `skydis.py`: `id:34094 200`, `id:34761 300`,
`id:34764 120`, `id:34843 160`, `id:34665 180`. Addresses below are RVAs.

| Function | Evidence | Consequence |
| --- | --- | --- |
| 34094, 5AEB80 | Takes ActiveEffect in rcx; calls controller art/shader slots at 5AEBAC/5AEBBC, gate40508 at 5AEBCB, sets visuals target at 5AEBE8, prepares hitEffects list at +0x60, calls34761 at 5AECE2. | Restarts visuals through the existing hook, with no gameplay Start/Finish, magnitude or duration change. Existing visual refs must be removed first: this function only clears their list nodes. |
| 34761, 5CC630 | Its second argument is used as a 16-byte list; reads/writes +8 at 5CC7A2/5CC7AA. Both art and shader can be inserted. | CommonLib's Start(ReferenceEffect**) declaration does not represent the output container correctly. Do not supply the address of one local pointer. Use34094 to prepare the engine list. |
| ReferenceEffectController::Stop | CommonLib implementation locks ProcessLists.magicEffectsLock, resets manager smart pointers whose controller matches. | Retain NiPointers first; reset alone need not destroy a ref held elsewhere. |
| 34764, 5CCA70 | Detach calls derived DetachImpl, controller.RemoveHitEffect(this), then clears controller; ownController alone permits controller deletion. | Use only non-owning refs of the embedded controller. Call Stop before Detach, while controller still matches. |
| 34843, 5CDCE0 | Removes this precise ReferenceEffect pointer from ActiveEffect.hitEffects. | Detach from a separate vector, never while iterating the changing list. |
| 34665, 5C8C60 | Model DetachImpl cancels clone work at 5C8C9F; clears cloneTask at 5C8CB5 and artObject3D at 5C8D11 after detaching geometry. | Explicit Detach also clears a model held by another NiPointer. ClearTarget is unnecessary; retain the target as in normal destruction. |

The captured MRE Detach virtual slot already points outside the executable. Calling virtual Detach
preserves that chain, but the other DLL's runtime behaviour is not proven by this static inspection.
The alternative engine stop 34050 uses a hooked visitor whose body was unavailable; it is not called.

## Current change and bounds

The earlier refresh-only build addressed HasSpell=false but the user still saw the red effect.
The later record/log investigation found Fire Storm's secondary F3931 -> 225AE -> 22477 -> 22471
path. See [visual-scope.md](visual-scope.md): all MGEFs of catalog-fire roots are visual candidates,
and shared equip abilities require a currently selected visual owner without a competing selected
nonvisual owner. A private copy is required for equip art; untracked runtime art recolouring is refused.

On AE 1.6.1170 both listed and unlisted abilities use the same visual-only path:

1. Re-read live effects; require the matching ability, player target, nonzero player caster,
   active/not-dispelled flags and the effect's own controller with art or shader.
2. Retain that controller's existing ReferenceEffects under the manager lock; reject owned controllers.
   Require current visual ownership OR a previous copy/our burn shader that needs restoration.
3. Outside the lock, Stop, mark refs finished and Detach. Retained pointers keep refs alive.
   Any remaining hitEffects or controller links prevent restart and produce a warning.
4. Invoke 34094 on the same gameplay ActiveEffect. Existing getters choose eligible copies/shader
   while stoked, or original assets when the buff or ownership no longer permits recolouring.

Buff edges and selected-spell/world changes queue refreshes. Getter eligibility reads current
selectedSpells directly, so a change is not accepted using the prior tick's cached classification.
Queued requests recheck queue installation, game thread, generation and buff state. No raw effect
pointer crosses the queue. RemoveSpell/AddSpell, gameplay dispel/recast and shared record edits are
not used. Missing 3D, absent visuals, foreign ownership or an unsupported runtime skips refresh.
Creation may be asynchronous or rejected: `visual restart requested` is not an on-screen PASS.

## Validation status

The current offline harness copies production EquipAbilityRefresh.cpp, VisualEquipContext.cpp and
VisualEquipRules.h unchanged, verifies SHA256 equality, and compiles the CPPs separately with fake
engine interfaces. It passes 61 checks: live shared-owner decisions, nonfire/mixed restoration,
shader-only refresh, both buff edges, listed/unlisted gameplay preservation and queue/detach guards.
Run `tools/blackflame-equip-refresh-check/run.ps1` in a Visual Studio Developer PowerShell.
Additional current checks: 13 closure, 22 visual-set, 21 script-provenance and 20 pure ownership-policy cases pass.
Full native build and final incremental build passed; builder 770/0, cosave/policy 510/0 with one
junction group skipped for permission. Dev DLL/PDB deployment is hash-verified, builder/config unchanged.
In-game validation of this scope change is pending; these counts do not
establish the native ABI, lifetime, final hook return values, asynchronous attachment or pixel colour.

Historical validation of the earlier refresh-only build (not the current visual-scope change):
BuildRelease completed; builder 770/0 and cosave 490/0 with a junction group skipped for sandbox
permission. That build used process-local /Z7 and a writable TEMP/TMP; the cache flag was restored.
Its game run confirmed visual restarts, but the user reported the same red appearance.

## User-run game checks

Equip Fire Storm before the lantern, then toggle lantern on/off/on, holding each state 2–3 seconds.
Compare the same view before/after charging and casting. Switch to a nonfire ritual sharing the red
body ability, then back to Fire Storm; shared art must stay original for the nonfire owner. If the
load order permits simultaneous visual/nonvisual owners of one ability, keep original in that mixed
context. Unrelated nonfire spells in the other hand must not veto a uniquely owned visual ability.
Check NPC equivalents, save/load, rapid switching and complete unequip for orphan art.

Logs should show old refs detached and visual restart requested with gameplay kept. Art identity
and later 3D attachment are separate evidence: immediate `after` may have no 3D yet. Check both
Fire Storm's 080E32 and the secondary 22471 copy. No lingering-link warning or NPC recolouring is
expected. Remaining red pixels still require their actual art/shader/light source to be identified.
