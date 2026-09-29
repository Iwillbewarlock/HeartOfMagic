#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// =============================================================================
// SPELL TOME HOOK SITE - may the tome hook patch this TESObjectBOOK::Read?
// =============================================================================
//
// No logging and no game types, so an offline check can compile it as it is
// and run it on the bytes of a real SkyrimSE.exe; SpellTomeHook.cpp resolves
// the addresses and logs which check failed.
//
// The hook replaces 0x56 bytes at the patch site (the player load, the
// AddSpell call and what follows up to the "book already read" test) and jumps
// back to site + return offset. So before it goes in, the code has to be the
// layout that was measured:
//   SE: mov rcx,[rip+disp32]; call rel32 at +0xE8 (the check tested in game on
//       1.5.97; no 1.5.97 exe was at hand to measure more).
//   AE: at +0x11D mov rcx,[rip+disp32]; call rel32, and
//       - the call goes to Actor::AddSpell (RELOCATION_ID(37771, 38716)), so
//         the site really is the spell-teach call on this build;
//       - the replaced block ends where an instruction starts: test byte
//         [r15+0x110], 8 (41 F6 87 ..) at site + 0x56;
//       - the return lands where an instruction starts: xor al, al (32 C0) at
//         site + 0x72.
// Anything else leaves the hook out, and tomes work the vanilla way.
//
// Measured offline on AE 1.7.104 (SkyrimSE.exe file version 1.7.104.0; its
// .text is not encrypted on disk), disassembled with Capstone. ID 17842 is
// RVA 0x280290 in the 1.7.99 address library (CommonLibSSE-NG
// tests/REL/versionlib-1-7-99-0.bin); aligning the IDs around it with the
// 1.7.104 .pdata function table gives a shift of 0 there, and 0x280290 is a
// function start of size 0x249. Offsets from the function start:
//   +0x01F mov r15, rcx                     the book
//   +0x10E mov rbp, [r15+0x118] .. +0x11A mov rdx, rbp   the spell, in rdx
//   +0x11D 48 8B 0D disp32                  mov rcx, [rip+..] (the player)
//   +0x124 E8 rel32 -> 0x6D3D60             Actor::AddSpell (ID 38716; the
//          1.7.99 library gives 0x6D3B00, shifted by +0x260 in 1.7.104 by the
//          same alignment, so 1.7.99 is a different layout there)
//   +0x129 0F B6 F0                         movzx esi, al
//   +0x173 41 F6 87 10 01 00 00 08          test byte [r15+0x110], 8 (block end)
//   +0x18F 32 C0                            xor al, al (return site)
// After +0x18F esi is only read: test sil, sil at +0x1D4 (which message) and
// movzx eax, sil at +0x22C, Read's return value. Read's callers (e.g. the one
// at 0x945D97) test al and, when it is set, call the player's vfunc 0x56
// (TESObjectREFR::RemoveItem) with count 1: the book is used up. The patch
// clears rsi, so Read returns false and the book stays.
// AE 1.6.318 and 1.6.1170 were tested in game with the +0x11D / +0x72 layout;
// their code could not be read offline here (no 1.6.318 exe; every 1.6.1170
// Steam exe on the machine has its .text encrypted on disk).
namespace SpellTomeSites
{
    inline constexpr std::size_t kPatchSize = 0x56;

    enum class BookRegister
    {
        Rdi,  // SE
        R15   // AE
    };

    struct Layout
    {
        std::ptrdiff_t siteOffset;    // from the start of TESObjectBOOK::Read
        std::ptrdiff_t returnOffset;  // from the patch site
        BookRegister   book;          // where the book pointer is at the site
    };

    inline constexpr Layout kSELayout{ 0xE8, 0x70, BookRegister::Rdi };
    inline constexpr Layout kAELayout{ 0x11D, 0x72, BookRegister::R15 };

    // Offsets inside the site pattern: mov rcx, [rip+disp32] (7 bytes); call rel32 (5 bytes)
    inline constexpr std::ptrdiff_t kCallOffset = 7;
    inline constexpr std::ptrdiff_t kCallRel32Offset = kCallOffset + 1;
    inline constexpr std::ptrdiff_t kCallEnd = kCallOffset + 5;

    // AE: the instruction right after the replaced block, and the one at the return site
    inline constexpr std::uint8_t kAEBlockEnd[] = { 0x41, 0xF6, 0x87 };  // test byte [r15+disp32], imm8
    inline constexpr std::uint8_t kAEReturn[] = { 0x32, 0xC0 };          // xor al, al

    enum class Check
    {
        Ok,
        SitePattern,  // no mov rcx, [rip+..]; call at the site (another mod was there first?)
        CallTarget,   // the call does not go to Actor::AddSpell (a different layout)
        BlockEnd,     // no instruction starts where the replaced block ends
        ReturnSite    // the return site is not the measured instruction
    };

    // Where the call at the site goes. a_siteAddress is the site's address in
    // the running game (or its RVA offline), a_site its bytes.
    [[nodiscard]] inline std::uintptr_t CallTarget(const std::uint8_t* a_site, std::uintptr_t a_siteAddress) noexcept
    {
        std::int32_t rel32 = 0;
        std::memcpy(&rel32, a_site + kCallRel32Offset, sizeof(rel32));
        return a_siteAddress + kCallEnd + static_cast<std::intptr_t>(rel32);
    }

    [[nodiscard]] inline bool StartsWith(const std::uint8_t* a_bytes, const std::uint8_t* a_expected, std::size_t a_count) noexcept
    {
        return std::memcmp(a_bytes, a_expected, a_count) == 0;
    }

    // a_func: the bytes of TESObjectBOOK::Read; a_funcAddress: its address (or
    // RVA); a_addSpellAddress: Actor::AddSpell's address (or RVA) on this build.
    [[nodiscard]] inline Check CheckSite(const std::uint8_t* a_func, std::uintptr_t a_funcAddress,
        bool a_isAE, std::uintptr_t a_addSpellAddress) noexcept
    {
        const auto& layout = a_isAE ? kAELayout : kSELayout;
        const auto* site = a_func + layout.siteOffset;
        if (!(site[0] == 0x48 && site[1] == 0x8B && site[2] == 0x0D && site[kCallOffset] == 0xE8)) {
            return Check::SitePattern;
        }
        if (!a_isAE) {
            return Check::Ok;  // SE keeps the check it was tested with
        }
        if (CallTarget(site, a_funcAddress + layout.siteOffset) != a_addSpellAddress) {
            return Check::CallTarget;
        }
        if (!StartsWith(site + kPatchSize, kAEBlockEnd, sizeof(kAEBlockEnd))) {
            return Check::BlockEnd;
        }
        if (!StartsWith(site + layout.returnOffset, kAEReturn, sizeof(kAEReturn))) {
            return Check::ReturnSite;
        }
        return Check::Ok;
    }
}
