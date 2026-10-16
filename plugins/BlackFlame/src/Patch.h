#pragma once

#include "Common.h"

#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <vector>

// =============================================================================
// PATCH - code checks and writes shared by the swaps (ImpactSwap, CopySwap*)
// =============================================================================
// Every Address Library id is checked in the running game's database first
// (HasIds, REL::Offset2ID) so a missing id is a log line, never a failed lookup.
// Call-site redirects go through SKSE::Trampoline write_call<5>; prologue detours
// through write_branch<5> plus a gateway this file builds in the same trampoline.

namespace BlackFlame::Patch
{
    inline constexpr std::uint8_t kCallOpcode = 0xE8;
    inline constexpr std::uint8_t kJmpOpcode = 0xE9;
    inline constexpr std::size_t kRel32Size = 5;

    /// The id of this runtime (SE or AE; VR is not built)
    [[nodiscard]] std::uint64_t IdFor(std::uint64_t a_se, std::uint64_t a_ae) noexcept;

    /// Whether the running game's Address Library has every id (each missing one logged with a_what)
    [[nodiscard]] bool HasIds(std::initializer_list<std::uint64_t> a_ids, const char* a_what);

    [[nodiscard]] std::uintptr_t AddressOf(std::uint64_t a_id);

    /// 1.6.1170 or 1.7.104: the builds whose code was read (1.7.104) or matched by pdata and unwind codes (1.6.1170).
    /// A fixed in-function offset is trusted only there; every other build finds its sites by scan and target check.
    [[nodiscard]] bool IsCodeReadBuild() noexcept;

    // Every hook's state for the tester file (Tester.h): installed or skipped, where, the bytes before, why
    struct HookRecord
    {
        std::string name;
        bool installed = false;
        std::uintptr_t address = 0;  // absolute; 0 when none was found
        std::string bytes;
        std::string note;
    };

    void RecordHook(std::string a_name, bool a_installed, std::uintptr_t a_address, std::string a_bytes, std::string a_note);
    [[nodiscard]] std::vector<HookRecord> HookRecords();

    /// vtable slot a_index of a_vtable to a_thunk, recorded; the old function
    template <class F>
    std::uintptr_t WriteVfunc(const char* a_name, REL::Relocation<std::uintptr_t>& a_vtable, std::size_t a_index, F a_thunk)
    {
        const auto slot = a_vtable.address() + a_index * sizeof(void*);
        const auto original = a_vtable.write_vfunc(a_index, a_thunk);
        RecordHook(a_name, true, slot, std::format("slot 0x{:X}, was {:X}", a_index, original), "vtable slot");
        return original;
    }

    /// a_size bytes at a_address are committed executable memory without a guard page
    [[nodiscard]] bool IsExecutable(std::uintptr_t a_address, std::size_t a_size);

    /// An SKSE / CommonLib trampoline stub (Trampoline.cpp TrampolineAssembly: FF 25 00000000, then the 8-byte
    /// destination) in committed executable memory: another plugin's redirect we may chain onto
    [[nodiscard]] bool IsTrampolineStub(std::uintptr_t a_address);

    /// The destination of the E8/E9 rel32 at a_site
    [[nodiscard]] std::uintptr_t Rel32Target(std::uintptr_t a_site);

    /// Every E8 in a_function's body (its function-table entry, or a fixed window) whose target is a_callee
    [[nodiscard]] std::vector<std::uintptr_t> FindCalls(std::uintptr_t a_function, std::uintptr_t a_callee);

    /// "48 89 4C 24 08 ..." (a_count bytes at a_address)
    [[nodiscard]] std::string HexBytes(std::uintptr_t a_address, std::size_t a_count);

    /// write_call<5> at a_site (an E8 checked by the caller), recorded with the bytes before; the old target
    template <class F>
    std::uintptr_t WriteCall(const char* a_name, std::uintptr_t a_site, F a_thunk, const std::string& a_note)
    {
        auto bytes = HexBytes(a_site, kRel32Size);
        const auto original = SKSE::GetTrampoline().write_call<5>(a_site, a_thunk);
        RecordHook(a_name, true, a_site, std::move(bytes), a_note);
        return original;
    }

    struct Detour
    {
        bool hooked = false;
        bool chained = false;          // onto another plugin's E9 / FF 25 at the prologue
        std::uintptr_t original = 0;   // what the thunk calls as the original (gateway or the chained target)
        std::size_t replayed = 0;      // prologue bytes moved into the gateway (0 when chained)
        std::string bytes;             // the first 16 bytes before the write
        std::string note;              // why it was skipped, or how it was hooked
    };

    /// The length of the position-independent prologue instructions covering at least 5 bytes, or 0 when one
    /// of them is not a form this decoder knows (push r, REX push r, mov [rsp+disp8], r64, sub rsp, imm, mov
    /// rax/r11, rsp). Exposed for the self-test's byte cases.
    [[nodiscard]] std::size_t ReplayLength(const std::uint8_t* a_bytes, std::size_t a_available) noexcept;

    /// Detours a_function's prologue to a_thunk: an E9 already there (another plugin) is chained onto (the
    /// thunk's original = that E9's target, which must be executable), an FF 25 absolute jump likewise; else
    /// the leading position-independent instructions (ReplayLength) are copied into a gateway that jumps back
    /// after them. Anything else is left alone (hooked false, bytes and reason in the result). Needs
    /// 14 + (a_replay + 14) trampoline bytes; checked first. a_originalSlot (what the thunk calls) is written
    /// before the jump is, so the thunk never runs with it unset.
    [[nodiscard]] Detour PrologueDetour(const char* a_name, std::uintptr_t a_function, std::uintptr_t a_thunk,
        std::uintptr_t& a_originalSlot);
}
