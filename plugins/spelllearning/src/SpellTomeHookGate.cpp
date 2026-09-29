#include "SpellTomeHookGate.h"
#include "SpellTomeHookSites.h"

// =============================================================================
// SPELL TOME HOOK GATE - may the tome hook patch this TESObjectBOOK::Read?
// =============================================================================
//
// Whether the patch may go in is decided by SpellTomeSites::Decide
// (SpellTomeHookSites.h: SE +0xE8 / AE +0x11D, 0x56 bytes, back at +0x70 /
// +0x72; the call there must stay inside the game's image; on AE it must go to
// AddSpell and both ends of the block must be the measured instructions, except
// that on the AE builds tested in game those three only warn). It used to be
// found by scanning 0x80-0x200 and taking the last match; when another mod had
// already rewritten the spot (Don't Eat Spell Tomes patches the very same
// place) that scan settled on an unrelated mov/call and overwrote engine code,
// and the game crashed on the first book read. Anything else now leaves the
// hook out: tomes then work the vanilla way.

namespace
{
    // Actor::AddSpell - the call the AE patch site must make.
    // Source: CommonLibSSE-NG src/RE/A/Actor.cpp — RELOCATION_ID(37771, 38716)
    constexpr REL::RelocationID AddSpellID(37771, 38716);

    constexpr std::string_view kVersionSeparator = ".";

    // SkyrimSE.exe's image in memory, [base, base + SizeOfImage), read from its
    // PE header the way REL::Module::load_segments reads the sections.
    SpellTomeSites::ImageRange GameImage()
    {
        const auto base = REL::Module::get().base();
        const auto* dosHeader = reinterpret_cast<const REX::W32::IMAGE_DOS_HEADER*>(base);
        const auto* ntHeader = SKSE::stl::adjust_pointer<const REX::W32::IMAGE_NT_HEADERS64>(dosHeader, dosHeader->lfanew);
        return { base, base + ntHeader->optionalHeader.imageSize };
    }

    // What a failed check found, for the log.
    std::string DescribeFailedCheck(SpellTomeSites::Check check, const std::uint8_t* site,
        std::uintptr_t siteAddress, const SpellTomeSites::Layout& layout, std::uintptr_t addSpell,
        SpellTomeSites::ImageRange image)
    {
        using SpellTomeSites::Check;
        using SpellTomeSites::kPatchSize;
        const auto callOffset = layout.siteOffset + SpellTomeSites::kCallOffset;
        switch (check) {
        case Check::SitePattern:
            return std::format("site pattern: the code at +{:X} is not mov rcx/call (found {:02X} {:02X} {:02X} .. {:02X})",
                layout.siteOffset, site[0], site[1], site[2], site[SpellTomeSites::kCallOffset]);
        case Check::CallOutsideImage:
            return std::format("call target: the call at +{:X} goes to {:X}, outside SkyrimSE.exe ({:X}-{:X})",
                callOffset, SpellTomeSites::CallTarget(site, siteAddress), image.begin, image.end);
        case Check::CallTarget:
            return std::format("call target: the call at +{:X} goes to {:X}, not to Actor::AddSpell ({:X})",
                callOffset, SpellTomeSites::CallTarget(site, siteAddress), addSpell);
        case Check::BlockEnd:
            return std::format("block end: no test byte [r15+..] at +{:X} (found {:02X} {:02X} {:02X})",
                layout.siteOffset + kPatchSize, site[kPatchSize], site[kPatchSize + 1], site[kPatchSize + 2]);
        case Check::ReturnSite:
            return std::format("return site: no xor al, al at +{:X} (found {:02X} {:02X})",
                layout.siteOffset + layout.returnOffset, site[layout.returnOffset], site[layout.returnOffset + 1]);
        default:
            return "none";
        }
    }
}

bool SpellTomeGate::SiteIsKnownLayout(std::uintptr_t funcBase, bool isAE)
{
    using SpellTomeSites::Check;
    using SpellTomeSites::Gate;
    const auto& layout = isAE ? SpellTomeSites::kAELayout : SpellTomeSites::kSELayout;
    const auto* func = reinterpret_cast<const std::uint8_t*>(funcBase);
    const std::uintptr_t addSpell = isAE ? AddSpellID.address() : 0;
    const auto image = GameImage();
    const auto version = REL::Module::get().version();
    const auto versionText = version.string(kVersionSeparator);
    const SpellTomeSites::RuntimeVersion runtime{ version.major(), version.minor(), version.patch() };
    const auto result = SpellTomeSites::Decide(func, funcBase, isAE, addSpell, image, runtime);
    if (result.gate == Gate::Install) {
        return true;
    }
    const auto what = DescribeFailedCheck(result.check, func + layout.siteOffset,
        funcBase + layout.siteOffset, layout, addSpell, image);
    if (result.gate == Gate::InstallWithWarning) {
        logger::warn("SpellTomeHook: {} is a build tested in game, so the hook goes in, but a newer check "
                     "failed - {}. This may be another mod or a layout that differs from the one measured; "
                     "please report this line",
            versionText, what);
        return true;
    }
    logger::error("SpellTomeHook: {}: check failed - {}", versionText, what);
    if (result.check == Check::CallOutsideImage) {
        logger::error("SpellTomeHook: another mod appears to have hooked the AddSpell call in TESObjectBOOK::Read, "
                      "so the tome hook stays out (it would overwrite that mod's hook) and spell tomes work the "
                      "vanilla way");
    } else {
        logger::error("SpellTomeHook: another mod has probably changed tome reading already (Don't Eat Spell "
                      "Tomes does), or this game version lays it out differently; the hook stays out and "
                      "spell tomes work the vanilla way");
    }
    return false;
}
