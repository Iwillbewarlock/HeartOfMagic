#pragma once

#include "Common.h"

#include <string>

// =============================================================================
// CONFIG - SKSE/Plugins/HeartOfMagic_BlackFlame.json
// =============================================================================
// Read once at plugin load, before any hook is installed or event sink
// registered; read-only afterwards, so every thread may read it.

namespace BlackFlame
{
    /// copyModelProbe (CopyModelProbe.h): "off" = no probe; "log" (the default) = probe and log only, never take a copy out
    /// (the in-game run of 46855dd reported 347 of 356 copy models failed with error 1 although they load in play: the probe's
    /// way of asking is in doubt); "drop" = a copy whose model fails while its source's loads is taken out of the swaps.
    /// A cached result (same copies map) is reused instead of probing, unless copyModelProbeAlways, selfTest or testerMode.
    enum class ProbeMode
    {
        kOff,
        kLog,
        kDrop
    };

    [[nodiscard]] const char* ProbeModeName(ProbeMode a_mode) noexcept;

    struct Config
    {
        bool burnShader = true;   // layer 1: the hit shader of the player's fire effects swapped for ours
        // C6: the runtime material recolour is retired; these three keep it, default off, until the copies that replace
        // them are proven in game (hitArt: E6 and E6-npc PASS; handArt: SetCastingArt calls on a real charge, and the SE
        // ids; boundWeapon: the bound-weapon copies). The Recolor.cpp material code goes with them.
        bool hitArt = false;      // layer 1: runtime recolour of the hit art's 3D on a later tick (off without the main-thread queue)
        bool handArt = false;     // layer 4: runtime recolour of the casting art in the player's hands
        bool boundWeapon = false; // layer 4: runtime recolour of the player's bound fire weapons' 3D
        bool scriptPlaced = true; // what scripts of the player's fire effects place or cast from (PapyrusWrap.h)
        bool impactSwap = true;   // the player's fire impacts load the grey copies of the grey map (ImpactSwap.h)
        bool hazardSwap = true;   // the player's fire hazards load the grey copies of the grey map (HazardSwap.h)
        bool light = true;        // projectile, explosion and casting lights
        // v2 copy swaps (CopySwap.h), each device on its own switch so the self-test can tell which worked
        bool copyLaunch = true;           // Projectile::Launch: the player's fire launch takes the projectile copy
        bool copyLaunchExplosion = true;  // after Launch: the projectile's explosion set to its copy
        bool copyExplosionCreate = true;  // explosion creation: the base set to its copy (projectile copy or self area)
        bool copyImpacts = true;          // GetImpactData inside a copy window returns the impact copy
        bool writeRequest = false;        // kDataLoaded: write the copy request and the NIF dump for the builder (CopyRequest.h);
                                          // J7: off for players (no NIF is read), on in the tester package
        bool copyPlaced = true;           // PlaceAtMe from the player's fire effect script: akFormToPlace becomes its copy
        bool copyCastingArt = true;       // SetCastingArt: the player's fire hand art becomes its copy (CopySwapArt.cpp)
        bool refreshCastingArt = true;    // the readied hands get the copy or original art at once when the buff turns on or off
                                          // (HandArtRefresh.h; needs copyCastingArt and the main-thread queue)
        bool copyHazardCreate = true;     // hazard creation: the base becomes its copy (copy window, spawn-hazard effect)
        bool copyHitArt = true;           // GetHitEffectArt (0x0C) returns the hit art's copy for the player's fire effects
        ProbeMode copyModelProbe = ProbeMode::kLog;  // v2i: the copy NIFs demanded from the model database in the background
                                                     // (CopyModelProbe.h); log only unless "drop"
        bool copyModelProbeAlways = false;           // probe at every start (the cached result is not reused)
        bool selfTest = false;    // run the scripted self-test once after a save is loaded (SelfTest.h)
        bool testerMode = false;  // the tester package: selfTest and writeRequest on, every log line written through at once,
                                  // and HeartOfMagic_BlackFlame_tester.txt (Tester.h; the file is written in every mode)
        // Dev only (DevAutomation.h): honoured only with selfTest on (testerMode counts); absent from the shipped json
        std::string devAutoLoad;   // a save name without extension: loaded once, 5 s after the main menu first opens
        bool devAutoE3 = false;    // E3's save and load done by the plugin
        bool devAutoQuit = false;  // the game quits 10 s after the self-test (and E3) ended, or 6 min after a load at most
        float selfTestDelaySeconds = 30.0f;  // C1: unpaused play after a load before the self-test starts (E3 judges after its own 1.5 s, H5)
        RE::NiColor lightColor{ 0.25f, 0.22f, 0.28f };
        // Single launch (README "Single launch: the DLL side"): the slot pool, the ledger, the builder as a child, publication
        // at kDataLoaded and at the main menu. Off: the copies map route above, unchanged
        bool singleLaunch = false;
        bool launchBuilder = true;    // with singleLaunch: the builder is started at kDataLoaded (BuilderLauncher.h)
        bool slotPreflight = false;   // kPreLoadGame: refill slots whose save pairs differ (unverified in game: off)
        bool selfTestSlots = false;   // self-test steps T0-slots and T0-W2 (with selfTest; SelfTestStepsSlots.cpp)
        bool slotTxst = false;        // decal texture set slots keyed, built and published (SlotTxst.h); off: the decals keep the source's
        double tint = 0.6;            // the copies' base colour scale (the builder's kDefaultTint), into the key and request v5
        double tintAdd = 0.9;         // additive palette shaders' base colour scale (kDefaultTintAdd)
        bool paletteGodskin = false;  // "paletteMode": "derived" (false, the builder's default) or "godskin"
        std::uint32_t builderJobMemoryMiB = 1536;  // the builder's job object memory cap (JOB_OBJECT_LIMIT_JOB_MEMORY)
    };

    inline constexpr const char* kConfigPath = "Data/SKSE/Plugins/HeartOfMagic_BlackFlame.json";

    /// Reads kConfigPath; a missing file, bad JSON or a bad value keeps the default
    /// for what was wrong and logs it.
    void LoadConfig();

    [[nodiscard]] const Config& GetConfig() noexcept;

    /// E6: SKSEPlugin_Load, when the main-thread queue could not be installed: the layers that change engine data from a
    /// task (the kept runtime recolours hitArt, handArt, boundWeapon, the light writes and the hand art refresh) are switched off for the
    /// session; the per-call swaps (copies, hit shader, impact and hazard models) keep working. Logs a_why.
    void DisableEngineWrites(const char* a_why);
}
