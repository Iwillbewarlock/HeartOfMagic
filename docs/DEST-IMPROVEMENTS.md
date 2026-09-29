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
// (the PlayerCharacter singleton load + AddSpell call), or the hook stays out.
const auto patchOffset = FindPatchSite(funcBase);
```

An earlier version scanned `0x80`–`0x200` and took the last match. That was
dropped: when another mod (Don't Eat Spell Tomes itself) has already rewritten
the site, the scan settled on an earlier, unrelated mov/call and overwrote
`0x56` bytes of engine code. An exact-position check cannot do that.

**Result:** One DLL works on SE 1.5.97, AE 1.6.x and AE 1.7.x. The offsets are
measured on SE 1.5.97, AE 1.6.318, AE 1.6.1170 and AE 1.7.104; any other build
gets the hook only when the pattern is at the same offset (see
[Key Offsets Reference](#key-offsets-reference)).

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
inline std::ptrdiff_t FindJumpOffset()
{
    return REL::Module::IsAE() ? 0x72 : 0x70;
}
```

It used to be hunted for by looking for a byte that often starts an
instruction (`0x48`, `0x40`, ...); such a byte also turns up inside
instructions, and jumping into the middle of one is a crash. The exact-position
check on the patch site is what keeps an unknown layout out.

---

## Problem 4 — Failure Mode

### DEST v1.2.2

```cpp
util::report_and_fail("Binary did not match expected, failed to install"sv);
// Game crashes to desktop. User sees nothing useful.
```

### Our Approach — Graceful Degradation

```cpp
const auto patchOffset = FindPatchSite(funcBase);
if (patchOffset < 0) {
    // FindPatchSite logged what it found and that another mod or a new
    // game layout is the likely reason
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
| **Patch site discovery** | Hardcoded offset | Measured offset per runtime, exact-position pattern check |
| **Jump offset** | Hardcoded | Measured offset per runtime |
| **AE sub-version support** | 1.6.318 only | 1.6.x and 1.7.x (measured: 1.6.318, 1.6.1170, 1.7.104) |
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
| Container-aware reading | Yes | Yes (same `Menu_mc` check) |
| Settings at runtime | — | Yes (from UI) |

---

## Key Offsets Reference

For future debugging — known working offsets across game versions:

| Game Version | Address Library ID | Patch Offset | Jump Offset | Book Register |
|--------------|--------------------|-------------|-------------|---------------|
| SE 1.5.97 | 17439 | `+0xE8` | `+0x70` | `rdi` |
| AE 1.6.318 | 17842 | `+0x11D` | `+0x72` | `r15` |
| AE 1.6.1170 | 17842 | `+0x11D` | `+0x72` | `r15` |
| AE 1.7.104 | 17842 | `+0x11D` | `+0x72` | `r15` |
| AE 1.6.640, 1.7.99 | 17842 | not measured: hooked only if the pattern is at `+0x11D` | `+0x72` | `r15` |

The code uses exactly these offsets (`IsAE()` picks the row); nothing is
scanned. How 1.7.104 was measured (offline, on the read-only exe, file version
1.7.104.0): the 1.7.99 address library in CommonLibSSE-NG
(`tests/REL/versionlib-1-7-99-0.bin`) puts ID 17842 at RVA `0x280290`; aligning
the IDs around it with the 1.7.104 `.pdata` function table gives a shift of 0
there (1603 of 2764 nearby IDs land on 1.7.104 function starts), and `0x280290`
is a function start of size `0x249`. Disassembled, `+0x1F` is `mov r15, rcx`
(the book), `+0x11A` `mov rdx, rbp` with `rbp = [book+0x118]` (the spell),
`+0x11D` `mov rcx, [rip+..]` and `+0x124` `call` Actor::AddSpell (ID 38716,
mapped the same way to `0x6D3D60`), `+0x129` `movzx esi, al` (the result that
decides consumption). The replaced block ends on an instruction at `+0x173`,
and `+0x11D + 0x72 = +0x18F` is an instruction start. The 1.6.1170 Steam exe
could not be re-read this way: its code section is encrypted on disk.

---

## Files

| File | Purpose |
|------|---------|
| `plugin/src/SpellTomeHook.h` | Hook class, settings struct, API |
| `plugin/src/SpellTomeHook.cpp` | Pattern scan, Xbyak patch, callback logic |

## Credits

- **Exit-9B** — Original DEST technique (MIT License)
- **alandtse** — CommonLibSSE-NG with `REL::RelocationID` support
- **DEST commit `18b81b1`** — SE register (`rdi`) reference
- **DEST commit `d874697`** — AE register (`r15`) and offset reference
