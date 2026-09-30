# DEST Improvements — SpellTomeHook vs Don't Eat Spell Tomes

How Heart of Magic's `SpellTomeHook` improves upon the original
[Don't Eat Spell Tomes](https://github.com/Exit-9B/Dont-Eat-Spell-Tomes) (DEST)
by Exit-9B.

---

## Background

DEST intercepts `TESObjectBOOK::Read` (aka `ProcessBook`) — the function Skyrim
calls when a player opens a spell tome. At a specific point inside that function
the game loads the `PlayerCharacter` singleton into `rcx` and calls `AddSpell`.
DEST NOPs out the entire spell-teach + book-consume region (0x56 bytes) and
replaces it with a small Xbyak patch that calls its own callback, then jumps past
the NOPd region.

Heart of Magic reuses this same fundamental technique but solves several
compatibility and robustness problems present in every released version of DEST
(v1.2.0 through v1.2.2).

---

## Problem 1 — Hardcoded Offsets Break on New Game Versions

### DEST's Approach (v1.2.1 / v1.2.2)

DEST uses **compile-time `#ifdef`** to separate SE/VR and AE code paths, with
hardcoded offsets baked into each:

```cpp
// DEST v1.2.2  —  Patches.cpp  (AE build, non-VR)
std::uintptr_t hookAddr = Offset::TESObjectBOOK::ProcessBook.address() + 0x11D;
//                                                                        ^^^^^
//                                     hardcoded for AE 1.6.318 ONLY

jmp(hookAddr.getAddress() + 0x72);   // hardcoded jump offset
```

```cpp
// DEST v1.2.2  —  Patches.cpp  (SE/VR build)
std::uintptr_t hookAddr = Offset::TESObjectBOOK::ProcessBook.address() + 0xE8;
//                                                                        ^^^^
//                                     hardcoded for SE 1.5.97 ONLY

jmp(hookAddr.getAddress() + 0x70);   // hardcoded jump offset
```

v1.2.2 added a pattern verification check, but on failure it calls
`util::report_and_fail()` — which **crashes the game to desktop**:

```cpp
auto pattern = REL::make_pattern<"48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??">();
if (!pattern.match(hookAddr)) {
    util::report_and_fail("Binary did not match expected, failed to install"sv);
    //                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //                     CTD if offset is wrong for your game version
}
```

**Result:** DEST only works on the exact game version it was compiled for.
Running the AE build on AE 1.6.1170 (or any version other than 1.6.318) triggers
the mismatch and crashes. There is no single DLL that works across SE and AE.

### Our Approach — Runtime ID, Fixed Offset, Exact-Position Check

We use `REL::RelocationID` to resolve the function base across SE and AE at
runtime (no compile-time split), then check that the pattern sits **exactly**
at the known offset for that runtime:

```cpp
// Single binary — resolves to the correct function on SE, AE 1.6 and AE 1.7
constexpr REL::RelocationID ProcessBookID(17439, 17842);
//                                        ^^^^^  ^^^^^
//                                        SE ID  AE ID (same ID on 1.7.x)

const std::uintptr_t funcBase = ProcessBookID.address();

// SE +0xE8, AE +0x11D. The bytes there must be
//   48 8B 0D xx xx xx xx E8 xx xx xx xx
//   mov rcx, [rip+disp32]; call rel32
// (the PlayerCharacter singleton load + AddSpell call), the call must stay
// inside SkyrimSE.exe's image (base .. base + SizeOfImage), and on AE also
//   - the call goes to Actor::AddSpell (RelocationID(37771, 38716).address())
//   - the replaced block ends on test byte [r15+..] (41 F6 87) at site + 0x56
//   - the return site, site + 0x72, is xor al, al (32 C0)
// or the hook stays out (SpellTomeSites::Decide, SpellTomeHookSites.h) -
// except on the AE builds tested in game (kInGameTestedAE: 1.6.318, 1.6.1170),
// where the site pattern and the in-image call decide and the last three
// checks only warn.
if (!SpellTomeGate::SiteIsKnownLayout(funcBase, isAE)) return false;  // SpellTomeHookGate.cpp
```

An earlier version scanned `0x80`–`0x200` and took the last match. That was
dropped: when another mod (Don't Eat Spell Tomes itself) has already rewritten
the site, the scan settled on an earlier, unrelated mov/call and overwrote
`0x56` bytes of engine code. An exact-position check cannot do that. The AE
checks beyond the pattern make an unmeasured build prove it has the measured
layout: the call target ties the site to AddSpell on that very build, and the
two instruction starts tie the block's size and the return offset to it. SE
keeps the pattern check it was tested with in game (no 1.5.97 exe was at hand
to measure its block end and return site). The log names the check that
failed.

On every build, SE included, the call at site + 7 must also land inside
SkyrimSE.exe's own image (`base` to `base + SizeOfImage`, read from the PE
header in `SpellTomeHookGate.cpp`). A vanilla `call rel32` never leaves the
image, so a target outside it is another SKSE mod that redirected the AddSpell
call to its own trampoline (`write_call<5>`); NOPing the block would silently
remove that mod's hook. The hook then stays out, tested build or not, and the
log says another mod appears to have hooked the AddSpell call in
`TESObjectBOOK::Read`, naming the target and the image range.

The AE builds the hook was tested on in game (`kInGameTestedAE`: 1.6.318 and
1.6.1170, where most players are) could not be read offline, so the newer
checks must not switch the hook off there: the site pattern and the in-image
call decide, and if the call goes elsewhere in the image than AddSpell, or the
block end or return site differs, the hook still goes in with a `WARNING`
naming the check and what was read (the call target, or the bytes) - which may
be another mod or a layout that differs from the one measured. Every other AE
build (1.6.640, 1.7.x, later) needs all five checks.

**Result:** One DLL works on SE 1.5.97, AE 1.6.x and AE 1.7.x. The hook was
tested in game on SE 1.5.97, AE 1.6.318 and AE 1.6.1170; on AE 1.7.104 it was
measured offline, not yet tested in game. Any build gets the hook only when the
checks above pass (see [Key Offsets Reference](#key-offsets-reference)).

---

## Problem 2 — SE/AE Register Difference

### DEST's Approach

DEST handles the register difference (`rdi` vs `r15` for the book pointer) via
compile-time `#ifdef SKYRIMVR`:

```cpp
#ifndef SKYRIMVR
    mov(rcx, r15);     // AE build only
#else
    mov(rcx, rdi);     // SE/VR build only
#endif
```

This means you need **two separate DLLs** — one for SE and one for AE.

### Our Approach — Runtime Detection

We check `REL::Module::IsAE()` at hook install time and generate the correct
Xbyak patch dynamically:

```cpp
const bool isAE = REL::Module::IsAE();

struct Patch : Xbyak::CodeGenerator
{
    Patch(std::uintptr_t a_callbackAddr, std::uintptr_t a_returnAddr, bool a_isAE)
    {
        if (a_isAE) {
            mov(rcx, r15);   // AE: book in r15
        } else {
            mov(rcx, rdi);   // SE: book in rdi
        }
        // ... rest of patch
    }
};
```

**Result:** Single DLL, single distribution, works on both SE and AE.

---

## Problem 3 — Jump Offset Varies Across Versions

### DEST's Approach

The jump offset (where execution resumes after the NOP region) is hardcoded:

| Version | Patch Offset | Jump Offset |
|---------|-------------|-------------|
| SE 1.5.97 | `+0xE8` | `+0x70` |
| AE 1.6.318 | `+0x11D` | `+0x72` |

If the compiler rearranges instructions (which happens between AE sub-versions),
the jump lands on the wrong instruction boundary and the game crashes.

### Our Approach — Measured Offsets

The jump offset is the measured one for the runtime (`+0x72` AE, `+0x70` SE):

```cpp
// SpellTomeHookSites.h
inline constexpr Layout kSELayout{ 0xE8, 0x70, BookRegister::Rdi };
inline constexpr Layout kAELayout{ 0x11D, 0x72, BookRegister::R15 };
```

It used to be hunted for by looking for a byte that often starts an
instruction (`0x48`, `0x40`, ...); such a byte also turns up inside
instructions, and jumping into the middle of one is a crash. On AE the site
check now also requires the measured instruction at the return site
(`xor al, al`, `32 C0`), so on an untested AE build where the jump would land
elsewhere the hook stays out. On the AE builds tested in game (1.6.318,
1.6.1170) a different return site is only a logged warning and the hook goes in,
as it did when those builds were tested. The block-end check (`41 F6 87` at
site + `0x56`) is a check that this is the measured layout, not something the
patched code relies on: after the patch the jump goes straight from the
trampoline to the return site, so site + `0x56` .. + `0x72` (`+0x173` ..
`+0x18F`) never runs. SE keeps the pattern check it was tested with, plus the
in-image call.

---

## Problem 4 — Failure Mode

### DEST v1.2.2

```cpp
util::report_and_fail("Binary did not match expected, failed to install"sv);
// Game crashes to desktop. User sees nothing useful.
```

### Our Approach — Graceful Degradation

```cpp
if (!SpellTomeGate::SiteIsKnownLayout(funcBase, isAE)) {
    // it logged which check failed (site pattern, call outside the image,
    // AddSpell call target, block end, return site) and what it found there
    return false;  // Hook not installed — game continues normally
}
```

If the check fails, the hook simply doesn't install. The game runs normally with
vanilla spell tome behavior. The log file explains exactly what happened.

---

## Summary Comparison

| Aspect | DEST v1.2.2 | Heart of Magic |
|--------|-------------|----------------|
| **SE + AE from one DLL** | No (separate builds) | Yes (`REL::RelocationID` + `IsAE()`) |
| **Patch site discovery** | Hardcoded offset | Measured offset per runtime, exact-position pattern check, the call kept inside the game's image (another mod's hook keeps ours out); on AE also the AddSpell call target and both block ends |
| **Jump offset** | Hardcoded | Measured offset per runtime, return site checked on AE |
| **AE sub-version support** | 1.6.318 only | 1.6.x and 1.7.x where the checks pass (tested in game: 1.6.318, 1.6.1170; measured offline: 1.7.104) |
| **Failure on unknown version** | CTD (`report_and_fail`) | Graceful fallback to vanilla |
| **Diagnostic logging** | Minimal | Full (func base, offset, jump, patch size) |
| **Book consumption** | Prevented (sets `rsi = 0`) | Same technique |
| **NOP region size** | `0x56` bytes | `0x56` bytes (same) |

---

## Additional Features Beyond DEST

DEST's scope is limited to preventing spell tome consumption and firing a
Papyrus event. Heart of Magic extends the hook callback with a full progression
system:

| Feature | DEST | Heart of Magic |
|---------|------|----------------|
| Prevent book consumption | Yes | Yes |
| Papyrus event on read | Yes | No (C++ callback) |
| XP grant on tome read | — | Yes (configurable %) |
| One-time XP (anti-exploit) | — | Yes (tracks per-spell) |
| Auto-set learning target | — | Yes |
| Skill level requirement | — | Yes (checks magic school) |
| Tree prerequisite system | — | Yes (hard + soft prereqs) |
| Tome inventory XP boost | — | Yes (bonus while carrying) |
| Already-known detection | — | Yes (notification + skip) |
| Vanilla mode toggle | — | Yes (instant learn fallback) |
| Voice slot spells (mods' script and animation spells) | — | Vanilla handling: learned at once, no ISL study, not in the tree (2026-09-30) |
| Container-aware reading | Yes | Yes (same `Menu_mc` check) |
| Settings at runtime | — | Yes (from UI) |

---

## Key Offsets Reference

For future debugging — known working offsets across game versions:

| Game Version | Address Library ID | Patch Offset | Jump Offset | Book Register | Known from |
|--------------|--------------------|-------------|-------------|---------------|------------|
| SE 1.5.97 | 17439 | `+0xE8` | `+0x70` | `rdi` | tested in game |
| AE 1.6.318 | 17842 | `+0x11D` | `+0x72` | `r15` | tested in game (DEST's reference build); site pattern and in-image call decide, newer checks warn |
| AE 1.6.1170 | 17842 | `+0x11D` | `+0x72` | `r15` | tested in game earlier; not re-read offline (its `.text` is encrypted on disk); site pattern and in-image call decide, newer checks warn |
| AE 1.7.104 | 17842 | `+0x11D` | `+0x72` | `r15` | measured offline, not yet tested in game |
| other AE (1.6.640, 1.7.99, ...) | 17842 | `+0x11D` | `+0x72` | `r15` | not measured: hooked only if every AE check passes |

(The last column is how each row is known.) The code uses exactly these offsets
(`IsAE()` picks the row); nothing is scanned. How 1.7.104 was measured
(offline, on the read-only exe, file version 1.7.104.0): the 1.7.99 address
library in CommonLibSSE-NG (`tests/REL/versionlib-1-7-99-0.bin`) puts ID 17842
at RVA `0x280290`; aligning the IDs around it with the 1.7.104 `.pdata`
function table gives a shift of 0 there (1603 of 2764 nearby IDs land on
1.7.104 function starts), and `0x280290` is a function start of size `0x249`.
Disassembled, `+0x1F` is `mov r15, rcx` (the book), `+0x11A` `mov rdx, rbp`
with `rbp = [book+0x118]` (the spell), `+0x11D` `mov rcx, [rip+..]` and
`+0x124` `call 0x6D3D60`. That is Actor::AddSpell (ID 38716): the 1.7.99
library gives `0x6D3B00`, and the same alignment shifts that stretch by
`+0x260` in 1.7.104, to `0x6D3D60` - so 1.7.99's layout differs around AddSpell,
and 1.7.99 stays unknown until the call-target check sees its real code.
`+0x129` is `movzx esi, al`. The replaced block ends on `41 F6 87 10 01 00 00 08`
(`test byte [r15+0x110], 8`) at `+0x173`, and the return site
`+0x11D + 0x72 = +0x18F` is `32 C0` (`xor al, al`).

Why clearing `rsi` keeps the book: after `+0x18F` the function only reads
`sil` (`test sil, sil` at `+0x1D4`, which message to show; `movzx eax, sil` at
`+0x22C`, the return value). Read's callers test that result: the one at
`0x945D97` does `test al, al` and, when set, calls the player's vfunc `0x56`
(`TESObjectREFR::RemoveItem`) with count 1 on the same PlayerCharacter
singleton that `+0x11D` loads (`0x3230778` in both). Read returning false means
the book is not removed.

---

## Files

| File | Purpose |
|------|---------|
| `plugins/spelllearning/include/SpellTomeHook.h` | Hook class, settings struct, API |
| `plugins/spelllearning/include/SpellTomeHookSites.h` | Offsets per runtime and the site checks (`CheckSite`), the in-game-tested list (`kInGameTestedAE`) and the decision (`Decide`), with the 1.7.104 measurement; no game types, so an offline check compiles it |
| `plugins/spelllearning/include/SpellTomeHookGate.h` | `SpellTomeGate::SiteIsKnownLayout`, the in-game side of the site check |
| `plugins/spelllearning/src/SpellTomeHookGate.cpp` | Site check and its log: resolves AddSpell and the image range (PE header), asks `SpellTomeSites::Decide`, logs a failed check |
| `plugins/spelllearning/src/SpellTomeHook.cpp` | Xbyak patch, callback logic |
| `plugins/spelllearning/src/SpellTomeHookInventory.cpp` | Tome inventory cache for the carried-tome XP boost |

## Credits

- **Exit-9B** — Original DEST technique (MIT License)
- **alandtse** — CommonLibSSE-NG with `REL::RelocationID` support
- **DEST commit `18b81b1`** — SE register (`rdi`) reference
- **DEST commit `d874697`** — AE register (`r15`) and offset reference
