#pragma once

#include "Common.h"

// =============================================================================
// PAPYRUS WRAP - script-placed fire of the player's fire effects (config scriptPlaced)
// =============================================================================
// ObjectReference.PlaceAtMe, Spell.Cast and Spell.RemoteCast keep their own native
// function: a proxy IFunction is bound in its place (IVirtualMachine::BindNativeMethod)
// that forwards every call and query to the original. Around the call, and only
// while the buff is on: when the CALLING frame's self (the script that called the
// native) resolves to an ActiveEffect (ActiveMagicEffect, VMTypeID 142) that is a
// fire effect the player cast, the reference PlaceAtMe returned is tagged placed,
// and the akSource of Cast/RemoteCast is tagged a source when the spell carries a
// fire MGEF (ScriptTags.h). A reference already tagged that calls them in turn (a
// placed activator's own script) counts too. A reference placed initially disabled
// has no 3D within the tag's window and stays as it is. No script, mod or FormID
// is named anywhere.

namespace BlackFlame
{
    /// SKSE Papyrus registration callback (SKSE::GetPapyrusInterface()->Register)
    bool InstallNativeWrappers(RE::BSScript::IVirtualMachine* a_vm);

    /// kPostLoadGame: binds what the registration callback could not find yet (each target once)
    void RetryNativeWrappers();
}
