// =============================================================================
// FileIndexGuardCheck - the structural checks that gate RegisterLoose (from FileProbeGuardCheck)
// =============================================================================
// IndexGuard's ctor and dtor are called only when they look like the guard Temp File Framework describes (FileIndex.h).
// Lifted from the spike's FileProbeGuardCheck.cpp (feature/black-flame-spike-s1 f79ae0c) unchanged but for the names.

#include "FileIndexGuardCheck.h"

#include "Patch.h"
#include "SehGuard.h"

#include <array>
#include <cstring>
#include <format>

namespace BlackFlame::FileIndexGuardCheck
{
    namespace
    {
        constexpr std::size_t kGuardSize = 0x200;      // the engine's guard (FileIndexEngine.h kGuardSize)
        constexpr std::size_t kGuardWindow = 0x80;     // bytes of IndexGuard's ctor / dtor scanned (FQ4)
        constexpr std::size_t kGuardBytesLogged = 32;
        constexpr std::size_t kMovRcxRipSize = 7;      // a REX.W mov with a rip-relative disp32
        constexpr int kRcx = 1;  // the register number of rcx (the guard pointer, the first integer argument)
        constexpr std::uint32_t kGuardManagerOffset = 0xB8;  // the manager inside the guard (TFF)
        constexpr std::array<std::uint8_t, 2> kFarJump{ 0xFF, 0x25 };  // jmp [rip + disp32]
        constexpr std::size_t kFarJumpSize = 6;

        std::uintptr_t Base() { return REL::Module::get().base(); }
    }
    // FQ4: IndexGuard's constructor and destructor must touch the ArchiveManager the way the guard's layout says (TFF: the
    // manager is kept at +0xB8 of the guard): within their first kGuardWindow bytes, a REX.W mov (8B / 89) that is either
    // rip-relative to the manager static or has a 32-bit displacement of +0xB8 (ModRM mod 10, with or without SIB).
    // A structural check, not a byte pattern: no byte pattern of the 1.6.1170 image is known here (its .text is encrypted
    // on disk). It cannot prove the functions right; it keeps two arbitrary functions from being called as the guard
    bool TouchesManager(std::uintptr_t a_function, std::uintptr_t a_manager)
    {
        if (!Patch::IsExecutable(a_function, kGuardWindow + kMovRcxRipSize)) {
            return false;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_function);
        for (std::size_t i = 0; i + kMovRcxRipSize < kGuardWindow; ++i) {
            const bool rexW = (bytes[i] & 0xF8) == 0x48;
            const bool mov = bytes[i + 1] == 0x8B || bytes[i + 1] == 0x89;
            if (!rexW || !mov) {
                continue;
            }
            const std::uint8_t modrm = bytes[i + 2];
            std::int32_t disp = 0;
            if ((modrm & 0xC7) == 0x05) {  // mod 00, rm 101: [rip + disp32]
                std::memcpy(&disp, bytes + i + 3, sizeof(disp));
                if (a_function + i + kMovRcxRipSize + disp == a_manager) {
                    return true;
                }
            } else if ((modrm & 0xC0) == 0x80) {  // mod 10: [reg + disp32], a SIB byte first when rm is 100
                const std::size_t at = (modrm & 0x07) == 0x04 ? i + 4 : i + 3;
                std::memcpy(&disp, bytes + at, sizeof(disp));
                if (disp == static_cast<std::int32_t>(kGuardManagerOffset)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Run 2 (1.6.1170): the dtor body reads its own argument (the guard): `48 8B D9` (mov rbx, rcx), then `8B 91 A8 00 00 00`
    // (mov edx, [rcx+0xA8]), a field inside the 0x200 guard. Accept the dtor when its body reads a field of the guard
    // pointer it was given (rcx, or a register rcx was copied to) at a displacement inside the guard, AND the ctor passed
    // the manager check. A mov r/m decode over the window: pass 1 marks registers that get a copy of rcx (REX.W 8B/89,
    // mod 3), pass 2 looks for a load (8B, mod 1/2) whose base is rcx or one of those, with 0 <= disp < 0x200.
    bool ReadsOwnGuard(std::uintptr_t a_function)
    {
        if (!Patch::IsExecutable(a_function, kGuardWindow + kMovRcxRipSize)) {
            return false;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_function);
        std::uint16_t aliases = 1u << kRcx;  // registers holding the guard pointer; rcx to start
        for (std::size_t i = 0; i + 3 < kGuardWindow; ++i) {
            if ((bytes[i] & 0xF8) != 0x48) {  // REX.W, no R/B extension (the observed move is 48 8B D9)
                continue;
            }
            const std::uint8_t op = bytes[i + 1];
            const std::uint8_t modrm = bytes[i + 2];
            if ((modrm & 0xC0) != 0xC0) {  // register-register only, for the alias copy
                continue;
            }
            const int reg = (modrm >> 3) & 0x07;
            const int rm = modrm & 0x07;
            if (op == 0x8B && (aliases & (1u << rm))) {  // mov reg, rm (rm the source)
                aliases |= static_cast<std::uint16_t>(1u << reg);
            } else if (op == 0x89 && (aliases & (1u << reg))) {  // mov rm, reg (reg the source)
                aliases |= static_cast<std::uint16_t>(1u << rm);
            }
        }
        for (std::size_t i = 0; i + kMovRcxRipSize < kGuardWindow; ++i) {
            const std::size_t op = (bytes[i] & 0xF8) == 0x48 ? i + 1 : i;  // an optional REX.W before the 8B
            if (bytes[op] != 0x8B) {
                continue;
            }
            const std::uint8_t modrm = bytes[op + 1];
            const int mod = modrm >> 6;
            const int rm = modrm & 0x07;
            if ((mod != 1 && mod != 2) || rm == 0x04 || rm == 0x05 || !(aliases & (1u << rm))) {
                continue;  // base rcx or an alias, with a displacement (no SIB, no rip)
            }
            std::int32_t disp = 0;
            if (mod == 1) {
                disp = static_cast<std::int8_t>(bytes[op + 2]);
            } else {
                std::memcpy(&disp, bytes + op + 2, sizeof(disp));
            }
            if (disp >= 0 && disp < static_cast<std::int32_t>(kGuardSize)) {
                return true;
            }
        }
        return false;
    }

    // Run 1 (1.6.1170): the dtor id points at a jmp thunk (E9 rel32, then CC padding). One leading E9 rel32, or FF 25
    // [rip + disp32] (an absolute jump through a slot), is followed to the body the check reads; a_function itself when
    // it starts with neither, or the jump's target cannot be read
    std::uintptr_t FollowJump(std::uintptr_t a_function)
    {
        if (!Patch::IsExecutable(a_function, kFarJumpSize)) {
            return a_function;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_function);
        if (bytes[0] == Patch::kJmpOpcode) {
            return Patch::Rel32Target(a_function);
        }
        if (bytes[0] == kFarJump[0] && bytes[1] == kFarJump[1]) {
            std::int32_t disp = 0;
            std::memcpy(&disp, bytes + kFarJump.size(), sizeof(disp));
            std::uintptr_t target = 0;
            const auto slot = a_function + kFarJumpSize + disp;
            if (GuardedKeep("file index jump slot", [&]() { target = *reinterpret_cast<const std::uintptr_t*>(slot); }) && target) {
                return target;
            }
        }
        return a_function;
    }

    std::string BodyText(std::uintptr_t a_function, std::uintptr_t a_body)
    {
        if (a_body == a_function) {
            return std::format("SkyrimSE+0x{:X} [{}]", a_function - Base(), Patch::HexBytes(a_function, kGuardBytesLogged));
        }
        return std::format("SkyrimSE+0x{:X} [{}] -> jump to SkyrimSE+0x{:X} [{}]", a_function - Base(),
            Patch::HexBytes(a_function, kGuardBytesLogged), a_body - Base(), Patch::HexBytes(a_body, kGuardBytesLogged));
    }
}
