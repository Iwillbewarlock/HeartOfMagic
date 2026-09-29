#pragma once

#include <cstdint>

// =============================================================================
// SPELL TOME HOOK GATE - the in-game side of SpellTomeSites::Decide
// =============================================================================
//
// Resolves AddSpell and the game's image range, asks SpellTomeSites::Decide
// (SpellTomeHookSites.h) whether the tome hook may patch TESObjectBOOK::Read at
// a_funcBase, and logs what a failed check found. Defined in
// SpellTomeHookGate.cpp.
namespace SpellTomeGate
{
    // true: the hook may go in (possibly with a logged warning); false: it
    // stays out and tomes work the vanilla way.
    [[nodiscard]] bool SiteIsKnownLayout(std::uintptr_t a_funcBase, bool a_isAE);
}
