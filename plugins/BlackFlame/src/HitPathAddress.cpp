#include "HitPathAddress.h"

#include "Patch.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace BlackFlame::HitPath::Detail
{
    namespace
    {
        constexpr std::size_t kReportBytes = 16;
        constexpr int kMaxHops = 8;
    }

    bool IsReadable(std::uintptr_t a_address, std::size_t a_size)
    {
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        auto at = a_address;
        const auto end = a_address + a_size;
        while (at < end) {
            MEMORY_BASIC_INFORMATION info{};
            if (at == 0 || VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT || (info.Protect & kReadable) == 0 || (info.Protect & PAGE_GUARD) != 0) {
                return false;
            }
            at = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        }
        return true;
    }

    std::string Hex(const std::uint8_t* a_bytes, std::size_t a_count)
    {
        std::string out;
        for (std::size_t i = 0; i < a_count; ++i) {
            out += std::format("{}{:02X}", i ? " " : "", a_bytes[i]);
        }
        return out;
    }

    std::string ModuleOffset(std::uintptr_t a_address)
    {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(a_address), &module) || !module) {
            return {};
        }
        std::array<wchar_t, MAX_PATH> path{};
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        std::wstring name = length ? std::wstring(path.data(), length) : std::wstring(L"(unnamed module)");
        if (const auto slash = name.find_last_of(L"\\/"); slash != std::wstring::npos) {
            name.erase(0, slash + 1);
        }
        std::string utf8;
        if (const int size = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), nullptr, 0,
                nullptr, nullptr); size > 0) {
            utf8.resize(static_cast<std::size_t>(size));
            (void)WideCharToMultiByte(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), utf8.data(), size, nullptr,
                nullptr);
        }
        return std::format("{}+0x{:X}", utf8, a_address - reinterpret_cast<std::uintptr_t>(module));
    }

    std::uintptr_t JumpTarget(std::uintptr_t a_at)
    {
        constexpr std::size_t kWindow = 16;
        if (!IsReadable(a_at, kWindow)) {
            return 0;
        }
        std::array<std::uint8_t, kWindow> b{};
        std::memcpy(b.data(), reinterpret_cast<const void*>(a_at), b.size());
        std::int32_t rel = 0;
        if (b[0] == 0xE9) {
            std::memcpy(&rel, &b[1], sizeof(rel));
            return a_at + 5 + rel;
        }
        if (b[0] == 0xEB) {
            return a_at + 2 + static_cast<std::int8_t>(b[1]);
        }
        if (b[0] == 0xFF && b[1] == 0x25) {
            std::memcpy(&rel, &b[2], sizeof(rel));
            const std::uintptr_t slot = a_at + 6 + rel;
            if (!IsReadable(slot, sizeof(std::uintptr_t))) {
                return 0;
            }
            std::uintptr_t to = 0;
            std::memcpy(&to, reinterpret_cast<const void*>(slot), sizeof(to));
            return to;
        }
        std::uintptr_t imm = 0;
        if (b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0) {  // mov rax, imm64; jmp rax
            std::memcpy(&imm, &b[2], sizeof(imm));
            return imm;
        }
        if (b[0] == 0x49 && b[1] == 0xBB && b[10] == 0x41 && b[11] == 0xFF && b[12] == 0xE3) {  // mov r11, imm64; jmp r11
            std::memcpy(&imm, &b[2], sizeof(imm));
            return imm;
        }
        if (b[0] == 0x68 && b[5] == 0xC7 && b[6] == 0x44 && b[7] == 0x24 && b[8] == 0x04 && b[13] == 0xC3) {
            std::uint32_t lo = 0, hi = 0;  // push imm32; mov dword [rsp+4], imm32; ret
            std::memcpy(&lo, &b[1], sizeof(lo));
            std::memcpy(&hi, &b[9], sizeof(hi));
            return (static_cast<std::uintptr_t>(hi) << 32) | lo;
        }
        return 0;
    }

    void ReportJumpChain(std::uint64_t a_id, const char* a_name)
    {
        const auto base = REL::Module::get().base();
        const auto entry = Patch::AddressOf(a_id);
        const auto rva = entry - base;
        if (!IsReadable(entry, kReportBytes)) {
            logger::warn("BlackFlame: hit path foreign hooks: {} {} at +0x{:X}: not readable memory", a_id, a_name, rva);
            return;
        }
        std::array<std::uint8_t, kReportBytes> bytes{};
        std::memcpy(bytes.data(), reinterpret_cast<const void*>(entry), bytes.size());
        std::vector<std::uintptr_t> hops;
        for (auto at = entry; static_cast<int>(hops.size()) < kMaxHops;) {
            const auto to = JumpTarget(at);
            if (to == 0 || std::ranges::find(hops, to) != hops.end() || to == entry) {
                break;
            }
            hops.push_back(to);
            at = to;
        }
        if (hops.empty()) {
            logger::info("BlackFlame: hit path foreign hooks: {} {} at SkyrimSE.exe+0x{:X}: {}: not hooked", a_id, a_name, rva,
                Hex(bytes.data(), bytes.size()));
            return;
        }
        std::string chain;
        for (const auto hop : hops) {
            const auto where = ModuleOffset(hop);
            chain += std::format(" -> {:X} ({})", hop, where.empty() ? "no module" : where);
        }
        const auto last = hops.back();
        const auto owner = ModuleOffset(last);
        HMODULE lastModule = nullptr;
        (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(last), &lastModule);
        std::string verdict;
        if (owner.empty()) {
            verdict = std::format("jumps to {:X}, memory no module owns (a trampoline or allocated code)", last);
        } else if (reinterpret_cast<std::uintptr_t>(lastModule) == base) {
            verdict = std::format("jumps within SkyrimSE.exe to +0x{:X} (the engine's own thunk, or a hook's gateway placed "
                                  "there)", last - base);
        } else {
            const auto plus = owner.rfind('+');
            verdict = std::format("hooked by {} at {}", owner.substr(0, plus), owner);
        }
        logger::info("BlackFlame: hit path foreign hooks: {} {} at SkyrimSE.exe+0x{:X}: {}: {} (chain{})", a_id, a_name, rva,
            Hex(bytes.data(), bytes.size()), verdict, chain);
    }

    bool VerifyCallSite(const char* a_name, std::uintptr_t a_site, std::uintptr_t a_callee)
    {
        const auto base = REL::Module::get().base();
        std::string why;
        if (!Patch::IsExecutable(a_site, Patch::kRel32Size)) {
            why = "not readable executable memory";
        } else if (*reinterpret_cast<const std::uint8_t*>(a_site) != Patch::kCallOpcode) {
            why = "not an E8 call";
        } else if (const auto target = Patch::Rel32Target(a_site); target != a_callee) {
            const auto where = ModuleOffset(target);
            why = std::format("an E8 to {:X} ({}), not to the expected callee at +0x{:X} (another plugin redirected it?)",
                target, where.empty() ? "no module" : where, a_callee - base);
        }
        const auto bytes = Patch::HexBytes(a_site, Patch::kRel32Size);
        if (!why.empty()) {
            logger::warn("BlackFlame: hit path observation: {} at SkyrimSE.exe+0x{:X}: {} (bytes {}) - skipped", a_name,
                a_site - base, why, bytes);
            Patch::RecordHook(a_name, false, a_site, bytes, why);
            return false;
        }
        logger::info("BlackFlame: hit path observation: {} at SkyrimSE.exe+0x{:X} verified (bytes {}, E8 to +0x{:X})", a_name,
            a_site - base, bytes, a_callee - base);
        return true;
    }
}
