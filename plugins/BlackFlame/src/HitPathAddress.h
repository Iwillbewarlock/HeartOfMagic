#pragma once

#include "Common.h"

#include <cstddef>
#include <cstdint>
#include <string>

// =============================================================================
// HIT PATH ADDRESS HELPERS - memory and code reads shared by the hit path observation
// (HitPathObserve.cpp), the corpse mark and its attach probe (CorpseMark.cpp) and the SRE
// probes (SreProbe.cpp). Read only; every read is checked with VirtualQuery first.
// =============================================================================

namespace BlackFlame::HitPath::Detail
{
    // OwnedController (2F0F18..2F0F7D): +0x08 target handle (34825 from the ref ApplyEffectShader runs on),
    // +0x10 the TESEffectShader, +0x20 the facing ref's handle. OwnedCameraEffectController (2F0E9C..2F0EC8): +0x10 = 0
    inline constexpr std::size_t kOwnedTargetHandle = 0x08;
    inline constexpr std::size_t kOwnedShader = 0x10;
    inline constexpr std::size_t kOwnedFacingHandle = 0x20;

    /// Committed, readable, no guard page, for all a_size bytes (across regions)
    [[nodiscard]] bool IsReadable(std::uintptr_t a_address, std::size_t a_size);

    /// "48 89 ..." of a_count bytes (the caller checked they are readable)
    [[nodiscard]] std::string Hex(const std::uint8_t* a_bytes, std::size_t a_count);

    /// "name.dll+0x1234", or empty when no module holds the address (VirtualAlloc'd memory: a trampoline)
    [[nodiscard]] std::string ModuleOffset(std::uintptr_t a_address);

    /// The destination of the jump at a_at (E9, EB, FF 25 [rip], mov rax/r11 imm64 + jmp, push + mov [rsp+4] + ret), or 0
    [[nodiscard]] std::uintptr_t JumpTarget(std::uintptr_t a_at);

    /// One "hit path foreign hooks" line for the function a_id (its first bytes and where a jump chain at its entry ends)
    void ReportJumpChain(std::uint64_t a_id, const char* a_name);

    /// An E8 at a_site whose target is a_callee (logged "verified"); else the reason is logged and recorded and false
    [[nodiscard]] bool VerifyCallSite(const char* a_name, std::uintptr_t a_site, std::uintptr_t a_callee);
}
