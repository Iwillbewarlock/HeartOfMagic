// ==========================================================================
// DummyDEST — Inert DontEatSpellTomes.dll replacement
// ==========================================================================
// This SKSE plugin does NOTHING.  It exists to replace ISL's real
// DontEatSpellTomes.dll via MO2 file conflict so that ISL's hook
// doesn't clash with Heart of Magic's SpellTomeHook.
//
// SpellLearning.dll provides the DEST API's native Papyrus functions
// (DESTPapyrusApi.cpp), so ISL and other DEST scripts work unchanged. It
// tells this shim from the real DLL by the export below.
// ==========================================================================

#include "Common.h"

// Asked for by name (DESTIntegration::RealDESTLoaded): present only in the shim
extern "C" __declspec(dllexport) bool HeartOfMagic_IsDESTShim() { return true; }

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse, false);
    SetupLog(
        "DontEatSpellTomes (Heart of Magic shim): Loaded. "
        "This is an inert replacement - SpellLearning.dll provides DEST API.");

    return true;
}
