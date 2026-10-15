#include "Patch.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#ifdef GetObject
#    undef GetObject
#endif

namespace BlackFlame::Patch
{
    namespace
    {
        constexpr std::size_t kFallbackScanBytes = 0x1000;  // when the function table has no entry
        constexpr std::size_t kStubSize = 14;               // FF 25 00000000 + 8-byte address
        constexpr std::size_t kPrologueWindow = 16;
        constexpr std::uint8_t kNop = 0x90;
        constexpr std::array<std::uint8_t, 6> kJmpRip{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    }

    std::uint64_t IdFor(std::uint64_t a_se, std::uint64_t a_ae) noexcept
    {
        return REL::Module::IsAE() ? a_ae : a_se;
    }

    bool HasIds(std::initializer_list<std::uint64_t> a_ids, const char* a_what)
    {
        const REL::Offset2ID table;
        std::vector<std::uint64_t> ids;
        ids.reserve(table.size());
        for (const auto& entry : table) {
            ids.push_back(entry.id);
        }
        std::ranges::sort(ids);
        bool all = true;
        for (const auto id : a_ids) {
            if (!std::ranges::binary_search(ids, id)) {
                logger::warn("BlackFlame: {}: Address Library id {} is not in this game's database - left off", a_what, id);
                all = false;
            }
        }
        return all;
    }

    std::uintptr_t AddressOf(std::uint64_t a_id)
    {
        return REL::ID(a_id).address();
    }

    bool IsExecutable(std::uintptr_t a_address, std::size_t a_size)
    {
        constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        MEMORY_BASIC_INFORMATION info{};
        if (a_address == 0 ||
            VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & kExecutable) == 0 || (info.Protect & PAGE_GUARD) != 0) {
            return false;
        }
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        return a_address + a_size <= regionEnd;
    }

    bool IsTrampolineStub(std::uintptr_t a_address)
    {
        return IsExecutable(a_address, kStubSize) &&
               std::memcmp(reinterpret_cast<const void*>(a_address), kJmpRip.data(), kJmpRip.size()) == 0;
    }

    std::uintptr_t Rel32Target(std::uintptr_t a_site)
    {
        std::int32_t rel = 0;
        std::memcpy(&rel, reinterpret_cast<const void*>(a_site + 1), sizeof(rel));
        return a_site + kRel32Size + rel;
    }

    std::vector<std::uintptr_t> FindCalls(std::uintptr_t a_function, std::uintptr_t a_callee)
    {
        DWORD64 imageBase = 0;
        std::uintptr_t end = a_function + kFallbackScanBytes;
        if (const auto* entry = RtlLookupFunctionEntry(a_function, &imageBase, nullptr)) {
            end = static_cast<std::uintptr_t>(imageBase + entry->EndAddress);
        }
        std::vector<std::uintptr_t> sites;
        for (auto at = a_function; at + kRel32Size <= end; ++at) {
            if (*reinterpret_cast<const std::uint8_t*>(at) == kCallOpcode && Rel32Target(at) == a_callee) {
                sites.push_back(at);
            }
        }
        return sites;
    }

    std::string HexBytes(std::uintptr_t a_address, std::size_t a_count)
    {
        std::string out;
        if (!IsExecutable(a_address, a_count)) {
            return "(not readable executable memory)";
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_address);
        for (std::size_t i = 0; i < a_count; ++i) {
            out += std::format("{}{:02X}", i ? " " : "", bytes[i]);
        }
        return out;
    }

    std::size_t ReplayLength(const std::uint8_t* a_bytes, std::size_t a_available) noexcept
    {
        constexpr std::uint8_t kPushFirst = 0x50, kPushLast = 0x57;
        constexpr std::uint8_t kRexB = 0x41, kRex = 0x40, kRexW = 0x48, kRexWR = 0x4C;
        constexpr std::uint8_t kMovStore = 0x89, kMovLoad = 0x8B, kGrp1Imm8 = 0x83, kGrp1Imm32 = 0x81;
        constexpr std::uint8_t kSubRsp = 0xEC, kSibRsp = 0x24, kModRmMask = 0xC7, kRspDisp8 = 0x44;
        constexpr std::uint8_t kRaxRsp = 0xC4, kR11Rsp = 0xDC;
        std::size_t i = 0;
        const auto has = [&](std::size_t a_count) { return i + a_count <= a_available; };
        while (i < kRel32Size) {
            const std::uint8_t b = a_bytes[i];
            if (b >= kPushFirst && b <= kPushLast) {  // push r64
                i += 1;
            } else if ((b == kRex || b == kRexB) && has(2) && a_bytes[i + 1] >= kPushFirst && a_bytes[i + 1] <= kPushLast) {
                i += 2;  // push r64 with a REX prefix (40 53 push rbx, 41 54 push r12)
            } else if ((b == kRexW || b == kRexWR) && has(5) && a_bytes[i + 1] == kMovStore &&
                       (a_bytes[i + 2] & kModRmMask) == kRspDisp8 && a_bytes[i + 3] == kSibRsp) {
                i += 5;  // mov [rsp+disp8], r64 (48 89 4C 24 08)
            } else if (b == kRexW && has(4) && a_bytes[i + 1] == kGrp1Imm8 && a_bytes[i + 2] == kSubRsp) {
                i += 4;  // sub rsp, imm8
            } else if (b == kRexW && has(7) && a_bytes[i + 1] == kGrp1Imm32 && a_bytes[i + 2] == kSubRsp) {
                i += 7;  // sub rsp, imm32
            } else if ((b == kRexW || b == kRexWR) && has(3) && a_bytes[i + 1] == kMovLoad &&
                       (a_bytes[i + 2] == kRaxRsp || a_bytes[i + 2] == kR11Rsp)) {
                i += 3;  // mov rax, rsp / mov r11, rsp
            } else {
                return 0;
            }
        }
        return i;
    }

    Detour PrologueDetour(const char* a_name, std::uintptr_t a_function, std::uintptr_t a_thunk,
        std::uintptr_t& a_originalSlot)
    {
        Detour result;
        if (!IsExecutable(a_function, kPrologueWindow)) {
            result.note = "the function is not readable executable memory";
            return result;
        }
        result.bytes = HexBytes(a_function, kPrologueWindow);
        auto& trampoline = SKSE::GetTrampoline();
        const auto* code = reinterpret_cast<const std::uint8_t*>(a_function);
        std::uintptr_t chainTo = 0;
        if (code[0] == kJmpOpcode) {
            chainTo = Rel32Target(a_function);
        } else if (std::memcmp(code, kJmpRip.data(), kJmpRip.size()) == 0) {
            std::memcpy(&chainTo, code + kJmpRip.size(), sizeof(chainTo));
        }
        if (chainTo != 0) {
            if (!IsExecutable(chainTo, 1)) {
                result.note = std::format("another plugin's jump at the prologue leads to {:X}, not executable memory - "
                                          "left alone", chainTo);
                return result;
            }
            if (trampoline.free_size() < kStubSize) {
                result.note = "the SKSE trampoline has no room - left alone";
                return result;
            }
            result.original = chainTo;
            result.chained = true;
            result.note = std::format("chained onto another plugin's jump to {:X}", chainTo);
        } else {
            const std::size_t replay = ReplayLength(code, kPrologueWindow);
            if (replay == 0) {
                result.note = "the prologue holds an instruction the replay decoder does not know - left alone";
                return result;
            }
            if (trampoline.free_size() < kStubSize + replay + kStubSize) {
                result.note = "the SKSE trampoline has no room - left alone";
                return result;
            }
            auto* gateway = static_cast<std::uint8_t*>(trampoline.allocate(replay + kStubSize));
            std::memcpy(gateway, code, replay);
            std::memcpy(gateway + replay, kJmpRip.data(), kJmpRip.size());
            const std::uintptr_t back = a_function + replay;
            std::memcpy(gateway + replay + kJmpRip.size(), &back, sizeof(back));
            result.original = reinterpret_cast<std::uintptr_t>(gateway);
            result.replayed = replay;
            result.note = std::format("{} prologue byte(s) replayed in a gateway", replay);
        }
        const std::string before = result.bytes;
        std::array<std::uint8_t, kPrologueWindow> original{};
        std::memcpy(original.data(), code, original.size());
        a_originalSlot = result.original;
        (void)trampoline.write_branch<5>(a_function, a_thunk);
        if (!result.chained && result.replayed > kRel32Size) {
            // Bytes of the moved instructions past the jump: never executed (the gateway returns after them)
            (void)REL::safe_fill(a_function + kRel32Size, kNop, result.replayed - kRel32Size,
                original.data() + kRel32Size, result.replayed - kRel32Size);
        }
        result.hooked = true;
        logger::info("BlackFlame: {} detoured at {:X} ({}); bytes before: {}", a_name, a_function - REL::Module::get().base(),
            result.note, before);
        return result;
    }
}
