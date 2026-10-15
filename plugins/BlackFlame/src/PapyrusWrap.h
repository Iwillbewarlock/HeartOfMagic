#pragma once

#include "Common.h"

#include <vector>

// =============================================================================
// PAPYRUS WRAP - script-placed fire of the player's fire effects (config scriptPlaced)
// =============================================================================
// ObjectReference.PlaceAtMe, Spell.Cast and Spell.RemoteCast keep their own native
// function: only when the original is an NF_util::NativeFunctionBase (checked
// through MSVC RTTI, skyrim_cast), a proxy deriving NativeFunctionBase copies its
// name, object and state name, return type, parameters, static, tasklet, latent
// and user flags and is bound in its place (IVirtualMachine::BindNativeMethod;
// "replaced" is logged only when the type then holds the proxy). The engine's own
// NativeFunctionBase::Call dispatches to the proxy's MarshallAndDispatch, which
// forwards to the original's and looks around it, only while the buff is on: the
// frame passed in is the native's own, its previousFrame the calling script.
// When that script is a fire effect the player cast (its self resolves to an
// ActiveEffect, VMTypeID 142), or a reference already tagged, PlaceAtMe's result
// (the result value) is tagged placed, and Cast/RemoteCast's akSource is tagged a
// source when the spell carries a fire MGEF (ScriptTags.h). The ActiveEffect is
// read only on the game thread: a call on a VM thread keeps the caller's VM
// handle and a task decides. Actors are never tagged. A reference placed
// initially disabled has no 3D within the tag's window and stays as it is. No
// script, mod or FormID is named anywhere.

namespace BlackFlame
{
    /// SKSE Papyrus registration callback (SKSE::GetPapyrusInterface()->Register)
    bool InstallNativeWrappers(RE::BSScript::IVirtualMachine* a_vm);

    /// kPostLoadGame / kNewGame: binds what the registration callback could not find yet (each once) and
    /// warns once when a wrapped target was rebound by another plugin
    void RetryNativeWrappers();

    struct WrapperStatus
    {
        const char* className = "";
        const char* functionName = "";
        bool bound = false;    // the script type holds our proxy now
        bool matches = false;  // the proxy's parameters and flags equal the original's
        std::uint32_t params = 0;
        bool latent = false;
    };

    /// For the self-test (T0-wrap): each target's state, read from the VM now
    [[nodiscard]] std::vector<WrapperStatus> CheckNativeWrappers();
}
