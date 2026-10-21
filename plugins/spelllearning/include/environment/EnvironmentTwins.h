#pragma once

#include "Common.h"
#include "environment/EnvironmentMatch.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

// =============================================================================
// EnvironmentTwins - the DLL side of HeartOfMagic.esp's explosion twins
//
// docs/ENVIRONMENT.md. At kDataLoaded the twin map (librarian/environment/
// twins.json) is read; each twin gets the live original's values (DATA, bounds,
// enchantment, image space modifier, model path - never its name or EditorID);
// every magic effect that carries the original and is, by record, a hidden
// harmless Script effect that no item gives a magnitude is recorded once, and
// pointed at the twin while config.json's environment.enabled is not false.
//
// Threads: game thread only, every function here. Its callers run there:
// kDataLoaded, the config apply task, and the scan's form reads (the panel's
// CollectScan task and Papyrus RunScan, which hops to the game thread). The
// scan's worker thread (PrepareScanText) reads no form and does not call it.
// =============================================================================

namespace Environment
{
    // The switch, read the way the adapters' requires.config reads it:
    // environment.enabled missing or unreadable means on.
    inline constexpr const char* kSwitchPath = "environment.enabled";

    // Game thread, kDataLoaded, after the perk adapters. Once per game session.
    void Initialize();

    // Game thread: the merged config.json the panel just saved. Repoints or
    // puts the originals back when the switch changed.
    void OnConfigApplied(const nlohmann::json& config);

    // The explosion a scan records for this effect - the original for an
    // effect pointed at a twin, whatever the switch; else its own.
    [[nodiscard]] RE::BGSExplosion* ScannedExplosion(const RE::EffectSetting* effect);

    // Frostwalker's pattern lists as Frostwalker reads them
    // (Data/SKSE/Plugins/Frostwalker/<folder>/, every file): four empty lists
    // when the folder is not there - Frostwalker has no built-in defaults, and
    // without it nothing reads the twin. `installed` says whether it was there.
    [[nodiscard]] FrostwalkerPatterns LoadFrostwalkerPatterns(bool& installed);

    // For the self-test (game thread)
    struct TwinInfo
    {
        std::string originalId;
        std::string twinId;
        RE::BGSExplosion* original = nullptr;   // null when not loaded
        RE::BGSExplosion* twin = nullptr;
        bool modelCopied = false;
    };
    [[nodiscard]] std::vector<TwinInfo> Twins();
    [[nodiscard]] bool SwitchOn();                       // the state last applied (config.json at kDataLoaded, then each settings save)
    [[nodiscard]] std::size_t RecordedEffectCount();
}
