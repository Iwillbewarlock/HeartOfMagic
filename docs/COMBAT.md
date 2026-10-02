# SL_Combat - the combat addon

`SL_Combat.dll` is a separate SKSE plugin next to Heart of Magic. It adds Hogwarts Legacy-style spell combat
built from the spells already in the load order. The design document (game-studio format, with the full
plan M0-M5) is the "SL_Combat 계획서" artifact; this file documents what is built.

| Milestone | What | State |
|---|---|---|
| M1 | Spell counter: when a spell is about to hit, a SkyPrompt prompt turns the counter key (the block key unless set) into the counter; a press in time raises the player's ward (dual-cast with spells in both hands and the perk), which takes the hit, stays up 1 s, and holding keeps it up | **built, not yet tested in game** |
| M2 | Spell colors (roles), chains, Opening/Down states | planned |
| M3 | Combat HUD | planned |
| M4 | Super gauge | planned |
| M5 | Compatibility checks, release packaging | planned |

**Requires SkyPrompt** (<https://www.nexusmods.com/skyrimspecialedition/mods/149963>; it needs SKSE Menu Framework
and ImGui Icons). Without it the addon logs `SkyPrompt is required` and installs nothing.

The addon is **not in the release archive** (`Distribution.cmake` does not copy it). `DeployDev.ps1` puts it in
the dev install; `-NoCombat` takes it out, and `-CombatOnly` copies only the addon (DLL, settings file the first
time) and leaves the panel and the main plugin in the install as they are.

## Files

| File | What it does |
|---|---|
| `plugins/Combat/src/Main.cpp` | Plugin entry (asks SKSE for a 14-byte trampoline); installs everything at `kDataLoaded` once SkyPrompt answers; clears state on load/new game |
| `plugins/Combat/src/CombatConfig.*` | Settings from `Data/SKSE/Plugins/SL_Combat.json` (every key has a default) |
| `plugins/Combat/src/IncomingWarning.*` | `MissileProjectile::UpdateImpl` hook: spell missiles about to hit the player (the nearest threat) |
| `plugins/Combat/src/CounterPrompt.*` | The SkyPrompt prompt on the counter key, its render-thread queue, the counter key presses |
| `plugins/Combat/src/CounterKey.*` | The counter key: the `counterKey` code, its bindings for the prompt, its name, matching its button events |
| `plugins/Combat/src/CounterInput.*` | The counter press (from the prompt) and whether the counter key is held |
| `plugins/Combat/src/ProjectileCounter.*` | The `MissileProjectile` collision hook, the counter decision, what a counter does with the missile |
| `plugins/Combat/src/CounterResponse.*` | Slow motion, the refunded next cast, learning XP through the Heart of Magic API |
| `plugins/Combat/src/WardSupport.*` | Known-ward check, raising the counter's ward (dual-cast), the held ward, 360 Ward detection |
| `plugins/Combat/src/CombatMenu.*` | The settings page in SKSE Menu Framework |
| `plugins/Combat/src/TestSpawn.*` | Test tools: hostile vanilla warlocks to practise on |
| `plugins/Combat/include/SkyPrompt/API.hpp` | SkyPrompt's MIT API header, copied (THIRD-PARTY-NOTICES) |
| `plugins/Combat/include/SKSEMenuFramework/SKSEMenuFramework.h` | SKSE Menu Framework's MIT API header, copied with a lazy DLL lookup (THIRD-PARTY-NOTICES) |
| `SKSE/Plugins/SL_Combat.json` | Default settings, copied to the dev install once |

## How the counter works

1. **Threat.** `IncomingWarning` hooks `MissileProjectile::UpdateImpl` (slot **0xAB**, `P/Projectile.h:211`).
   After the game's own update, an enemy spell missile is a threat when its straight-line path passes within
   `warnRadius` of the player's chest and reaches it within `warnLeadSeconds` (distance along the velocity /
   speed; spell missiles have no gravity), and the player knows a ward (`requireKnownWard`). The nearest threat
   is kept, and reported to `CounterPrompt`.
2. **Prompt = counter key.** While there is a threat, `CounterPrompt` shows one SkyPrompt prompt (`promptText`)
   on the **counter key** (`CounterKey`) when at least one hand holds a spell (`requireSpellInHand`, on by
   default; off, any equipment counters - a two-handed weapon, a bow, a sword and shield, fists):

   | `counterKey` | Keyboard / mouse | Gamepad |
   |---|---|---|
   | `-1` (default) | The game's block key, `"Left Attack/Block"` as the control map binds it (follows a remap) | Its block binding |
   | A code | That key (SkyUI code: scan code, or 256 + mouse button) | Its block binding |

   The prompt is attached to the player (reference 0x14), which SkyPrompt draws beside the head, and
   its progress ring runs down to the impact. While it shows, SkyPrompt takes that key: a press does not block or
   cast, it is the counter press (`kDown`, its time recorded at once). Outside that moment the key works as usual,
   so blocking or casting never counters. The prompt comes down 100 ms after the threats stop. The counter still
   needs a known ward; the ward it raises is the strongest known one, as always.
3. **Impact.** `ProjectileCounter` hooks `MissileProjectile` vtable slot **0xBE (190)**. When a spell missile's
   collision list has the player on one side of a hit pair, the counter press is checked:
   - it was at most `counterWindow` seconds ago (+`wardBonusWindow` when a hand holds a ward) - pressing as
     soon as the prompt appears (0.6 s out) is too early,
   - the player knows at least one ward spell,
   - the projectile came from within `frontAngle` degrees of the player's facing (measured from its velocity),
     or from anywhere with **360 Ward** (`360 Ward.esp`, ESL-flagged: looked up as a full or a light plugin) loaded (`sphereWardAllAngles`),
   - the shooter is not the player.
4. **Ward up.** A counter casts the player's strongest known ward on them right there (by Ward Power magnitude;
   `CastSpellImmediate` from the instant caster, no magicka charged) and lets the original collision go on: the
   game's own ward rules take the hit, and so do ward mods and 360 Ward's sphere, since it is the real ward
   spell (`counterResult: "ward"`). **Dual cast:** with spells in both hands and the Restoration dual-casting perk
   (`dualCastPerk`) the ward is cast with the game's dual-cast effectiveness (`fMagicDualCastingEffectivenessBase`);
   otherwise the plain ward. The linger and the hold follow the same rule at each refresh.
   Fallbacks behind the same setting, for the M1 test: `"absorb"` drops the hit and removes the missile with its
   explosion cleared; `"reflect"` sends it back at the shooter's chest.
   **Linger:** the counter's ward stays up `wardLingerSeconds` (1 s) even if the key is let go - re-cast every
   `holdRefreshSeconds`, free - so the player is safe for a moment after the counter.
5. **Response.** Posted as an SKSE task (the hook's calling thread is not known): slow motion, the free next cast
   (`freeCastWindow`; its magicka refunded 0.6 s after the cast) and `xpPerCounter` learning XP to that cast's
   spell through `AddSourcedXP(..., "combat")`.
6. **Hold.** If the counter key is still down a frame after the counter, a ward stays up while it is held,
   beyond the linger (`holdWard`): every `holdRefreshSeconds` the strongest known ward is cast on the player again
   (dual-cast by the rule above) and its per-second magicka cost (x `fMagicDualCastingCostMult` when dual)
   charged; released (prompt `kUp`, or the key's up event) or out of magicka, it stops; the linger, if still
   running, carries on free. One loop on the game thread does both: held key -> charged refresh, within the
   linger -> free refresh, neither -> the ward is let go (`counter.ward down`).

Only `MissileProjectile` is countered: firebolt, fireball, ice spike and other missiles. Beams (Lightning Bolt),
cones (breath shouts) and flames are not; what a held ward stops is left to the game and to ward mods.

### SkyPrompt calls go through the render thread

SkyPrompt draws inside the frame's Present call (`BSGraphics::Renderer::End`, `RELOCATION_ID(75461, 77246)` +
0x9 / 0x15 on AE). A prompt sent or removed from another thread while it draws crashed SkyPrompt 2.4.0 (CIGAR's
report, 2026-09-28). So, following CIGAR by kkww101010 (GPL-3.0, `github.com/kkw1010-dev/HKT`, `src/Prompt.cpp`),
every `SendPrompt`/`RemovePrompt` waits in a queue that a `write_call<5>` hook on that Present call drains, under
a lock; with no Present call for 5 s the queue is delivered from the calling thread instead. SkyPrompt reads
`GetPrompts()` on that same thread, and keeps `string_view`s into the text, so the previous text lives one more
round. `ProcessEvent` arrives on SkyPrompt's thread and touches atomics only; the ward release it causes is
posted as a task.

Unverified until the game is run: that SkyPrompt takes the counter key (the block key's mouse button, or a side
button) while the prompt shows, so the press does not also block or cast; that the ring
speed encoding (`speed + start`, SkyPrompt's "Progress Circle") runs out near the impact; that
`CastSpellImmediate` of a concentration ward raises Ward Power before the same hit is applied (else
`"counterResult": "absorb"`); that `0x153D1` is the Restoration dual-casting perk (the log names it at load).

### The 0xBE slot

CommonLibSSE-NG v10 labels slot 0xBE `bool HandleHits(hkpCollidable*)`. Elden Parry (2022) and its GPL-3 remake
(`chenans1/EldenParryRemake`) both hook it as `(Projectile*, hkpAllCdPointCollector*)` and read the collector's
`hits[].rootCollidableA/B`; that is what SL_Combat does. The hook is declared returning `bool`: the original's
result is passed through on the normal path, and `hookReturnOnCounter` (default `false`) is returned after a
counter, so the return value can be flipped from the settings file if a test shows it matters.

Elden Parry's source has no licence; only the technique (owner reset, filter rewrite, retarget - used by the
`"reflect"` fallback) is used, the code is written anew.

### Collision filter

Only the `"absorb"` and `"reflect"` fallbacks touch the filter. `filterMode` picks how the projectile stops
colliding with the player:

- `"shift"` (default): `filter = (filter & 0xFFFF) | (playerFilter << 16)` - the bit pattern both Elden Parry
  versions ship.
- `"systemGroup"`: `CFilter::SetSystemGroup(playerFilter.GetSystemGroup())`. Unproven; there for comparison
  in the M1 test.

The two give different bits: `"shift"` moves the player's whole low half (layer, biped part, flags) into the
system-group half, it does not copy the player's system group. It is kept because it is what works in Elden
Parry, not because it is understood.

### Slow motion

The game's time-multiplier **target** (`RELOCATION_ID(511883, 388443)`, the address
`BSTimer::QGlobalTimeMultiplierTarget()` reads) is set to `slowMoScale`, as Payload Interpreter does; the game
eases the current multiplier toward it. The value it had before is saved and put back - never forced to 1.0 -
after `slowMoDuration` real seconds (a sleeper thread posts the restore as an SKSE task) or when any menu opens,
but **only if the target still holds the value SL_Combat wrote**: if Slow Time or another mod changed it in
between, theirs stays and ours is dropped (`counter.slowmo left alone`). On `kPreLoadGame` / `kPostLoadGame` /
`kNewGame` nothing is written; the state and the armed free cast are just forgotten. A second counter during
slow motion extends it instead of stacking.

### Other parry mods

At load the addon logs which projectile-parry DLLs are present. Weapon/shield parry mods (`EldenParry.dll`,
`ParryAll.dll`, `DualWieldParryingNG.dll`, `ValhallaCombat.dll`) parry on a block. With the counter on the block
key the two meet only while the prompt shows - SkyPrompt then takes the key, so the game sees no block and the
counter wins; set `counterKey` to another key to keep them apart. Unverified in game. A ward-reflect mod (`PerfectlyValidWards.dll` -
the DLL name is unverified) reflects spells off a ward held in hand, so with it loaded the counter stands down
whenever either hand holds a ward (`skipIfOtherParryMod`).

## Settings page (SKSE Menu Framework)

With SKSE Menu Framework installed (SkyPrompt needs it anyway), the game's Menu Framework window has
**Heart of Magic Combat > Counter**. It shows the state (counter active, 360 Ward, the dual-cast perk's name, the
ward a counter would raise) and edits the settings below - the counter and its key (and whether it needs a spell
in hand), the ward, the reward, the
two texts. The key line names the current key; **Block key**, **Middle Mouse**, **Mouse 4**, **Mouse 5** set it
in one press, and **Key code** takes any code (an invalid one goes back to `-1`).
The page works on a copy: **Apply and save** hands it to the game thread, which makes it live and rewrites
`SL_Combat.json` with every key; **Revert** goes back to the live values; **Defaults** loads the built-in ones
(keep them with Apply). Turning the counter on takes a restart when it was off at load, since the hooks are
installed then; turning it off works at once. The labels are English (ImGui's font).

**Test tools** (`testTools`, off by default; the page's Test box shows them at once): buttons that place a vanilla
leveled warlock from Skyrim.esm about 700 units (10 m) in front of the player - `LvlWarlockFire` (0x01E7CD) and
`LvlWarlockFrost` (0x01E7CE) cast missiles, so they are counter practice; `LvlWarlockShock` (0x01E7CF) casts a beam,
a check that no prompt shows - and one that removes every mage placed this session (disabled and deleted). The
IDs were read from Skyrim.esm's records (EDIDs), not assumed. No plugin file is needed. Log lines `test.spawn`.

The header resolves `SKSEMenuFramework.dll` when a function is first used: upstream looked it up during static
initialisation, which finds nothing when SKSE loads SL_Combat first (CIGAR found and fixed the same).

## Settings (`SKSE/Plugins/SL_Combat.json`)

| Key | Default | Meaning |
|---|---|---|
| `enableCounter` | `true` | Off = no hook, no sinks |
| `counterKey` | `-1` | The counter key: `-1` the game's block key; else a SkyUI key code (keyboard scan code 1-255, 256 + mouse button: 258 middle, 259 / 260 side buttons). A gamepad always uses its block binding |
| `counterWindow` | `0.25` | Seconds before impact a counter press still counts |
| `wardBonusWindow` | `0.10` | Extra seconds when either hand holds a ward |
| `frontAngle` | `60` | Degrees either side of the facing; hits from behind are not countered |
| `skipIfOtherParryMod` | `true` | With a ward-reflect mod loaded, no counter while either hand holds a ward |
| `filterMode` | `"shift"` | `"shift"` or `"systemGroup"` (above) |
| `dualCastPerk` | `"Skyrim.esm\|0x153D1"` | With spells in both hands and this perk, the counter's (and the held) ward is dual-cast |
| `counterResult` | `"ward"` | `"ward"` (raise a ward, the game takes the hit), `"absorb"` or `"reflect"` (fallbacks above) |
| `hookReturnOnCounter` | `false` | Value the collision hook returns after an `"absorb"` / `"reflect"` counter |
| `slowMoScale` | `0.4` | Time multiplier during slow motion; `1` turns it off |
| `slowMoDuration` | `0.35` | Real seconds |
| `freeNextCast` | `true` | Refund the magicka of the next cast after a counter |
| `freeCastWindow` | `3.0` | Seconds the free cast stays available |
| `xpPerCounter` | `5` | Learning XP to the next cast's spell; `0` = none |
| `requireKnownWard` | `true` | The counter needs at least one known ward spell |
| `requireSpellInHand` | `true` | The prompt (and so the counter) needs a spell in at least one hand; `false` = any equipment |
| `wardLingerSeconds` | `1.0` | The counter's ward stays up this long even if the key is let go (free) |
| `holdWard` | `true` | Holding the counter key after a counter keeps a ward up |
| `holdRefreshSeconds` | `0.25` | How often the held ward is re-cast and its magicka charged |
| `sphereWardAllAngles` | `true` | With 360 Ward loaded, counters from every direction; without it `frontAngle` applies anyway |
| `warnLeadSeconds` | `0.6` | How early the prompt appears, in seconds before impact |
| `promptText` | `"Counter"` | The prompt's label (SkyPrompt also takes `$` translation keys) |
| `warnRadius` | `120` | A missile passing farther than this (game units) from the chest is ignored |
| `testTools` | `false` | Show the hostile-mage spawner on the settings page |
| `hudText` | `"Countered"` | Corner message `<hudText>: <spell>` after a counter (until the M3 HUD); `""` = none |

## Growth: Heart of Magic does it

SL_Combat has no progression of its own. It registers the XP source `combat` ("Combat (counter)") with
`RegisterXPSource` at `kDataLoaded` and grants XP with `AddSourcedXP` - the public API v1, the same way
`SL_BookXP` does - so Heart of Magic's settings panel shows the usual enable / multiplier / cap controls for it,
and XP, mastery and unlocks stay Heart of Magic's. Without Heart of Magic the counter still works, without XP.

## Log lines (`SL_Combat.log`)

| Line | When |
|---|---|
| `counter.impact spell=… shooter=… msSincePress=… ward=… knowsWard=… angle=… countered=…` | Every spell missile that hits the player |
| `counter.hit spell=… ward raised wardPower=…` (or `absorbed` / `reflected to …`) | A counter |
| `counter.slowmo scale=… saved=…` / `counter.slowmo restored=…` / `counter.slowmo left alone …` | Slow motion on / off / someone else changed the speed meanwhile |
| `counter.refund spell=… cost=…` | The free cast's magicka given back |
| `counter.ward hold start/refresh/end …` / `counter.ward down` | The held ward (refresh shows the cost charged and Ward Power) / the ward let go after the linger and hold |
| `counter.xp spell=… asked=… granted=…` | Learning XP sent to Heart of Magic |
| `counter.prompt show key=… bindings=… in=…` / `counter.prompt press` | The prompt up (the key's name, how many device bindings) / a counter-key press |
| `counter.ward raise ward=… dual=… effectiveness=…` | The counter's ward |

`msSincePress=-1` means no press was recorded. These lines turn the M1 checklist below into numbers.

## M1 in-game checklist

| # | Try | Expect |
|---|---|---|
| 1 | Counter a Firebolt-casting enemy 20 times, pressing just before impact | 18+ `countered=true`, a ward appears, `wardPower>0`, the bolt is absorbed by the ward (little or no damage) |
| 2 | Press late (after the hit) 10 times | All `countered=false`, normal damage |
| 3 | Repeat 1-2 with Ice Spike and **Fireball** (a missile with an explosion) | Same; the ward takes the explosion |
| 4 | Cast right after a counter | Spell fires; `counter.refund` with its cost; magicka back |
| 5 | During slow motion open the inventory, save, load | Speed back to normal every time (`counter.slowmo restored`) |
| 6 | Two enemies cast at once | One press counters one missile; no crash |
| 7 | Get hit from behind right after a press | `countered=false` (angle > `frontAngle`) |
| 8 | Repeat 1-6 on SE (1.5.97) and AE | Same results |
| 9 | Unlearn every ward (or a character without one), counter | `knowsWard=false`, no counter |
| 10 | Counter, keep holding | `counter.ward hold start`, a ward visible, magicka draining, `wardPower>0`; release ends it |
| 11 | With and without 360 Ward: counter a missile from behind | With: countered; without: `countered=false` |
| 12 | Face a Firebolt caster | The prompt appears beside the head about 0.6 s out with the block key, the ring runs down, gone after the hit |
| 13 | While the prompt shows, press the key early (as soon as it appears) | No block or cast (SkyPrompt takes the key), `countered=false` (too early) |
| 14 | Block or cast with no spell incoming | Block and spells work normally; no `counter.prompt press` |
| 15 | Spells in both hands, Restoration dual-casting perk, counter | `counter.ward raise … dual=true`, a stronger ward; at load the log names the perk |
| 16 | One spell, or without the perk | `dual=false`, the plain ward |
| 17 | A spell that will clearly miss | No prompt |
| 18 | Counter, let go at once | The ward stays about 1 s with no magicka spent, then `counter.ward down` |
| 19 | Two-handed sword, bow, sword and shield, fists (no spell) | No prompt; with `requireSpellInHand` off, the same prompt on the block key and a press in time counters |
| 20 | Settings page: set Mouse 4 (or a keyboard key), Apply | The key line names it; the next prompt shows that key, a press counters, the block key blocks as usual |
| 21 | Same with a gamepad | The prompt shows the gamepad's block button; it counters |
| 22 | Settings page: Block key, Apply | Back to the block key |

If `counter.hit … ward raised` shows `wardPower=0` or the full damage still lands, the ward came too late: try
`"counterResult": "absorb"`. In the fallbacks, if the missile hits the player again, try
`"filterMode": "systemGroup"`; if the hit still lands, `"hookReturnOnCounter": true`.
