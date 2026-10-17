#pragma once

#include "Common.h"

// =============================================================================
// FILE VISIBILITY PROBE - can the game load a NIF / DDS that appears during the session? (dev only, spike S1)
// =============================================================================
// Config "devFileVisibilityProbe": true (default off; absent from the shipped json). kMenuSettle after the main menu first
// opens, on the main thread, one step a frame (FileProbeRun.h, FileProbeTests.cpp):
//   Prepare     the engine scans (FileProbeEngine.h), old probe_* files removed from meshes\ and
//               textures\heartofmagic\blackflame\fileprobe\, a projectile NIF and a DDS it names read through the game's
//               file system (the bytes every test writes or serves), the source model Demanded and its DDS asked (controls)
//   PreRequest  the "-neg" paths Demanded / asked before their files exist, and a texture path no file ever has
//   WriteFiles  probe_<a|aneg|c|cneg>_<run>.nif / .dds written into <game>\Data (MO2: lands in overwrite through USVFS)
//   A           a loose file written this session: plain stream open, GlobalLocations open, index lookup, then
//               BSModelDB::Demand / BSShaderManager::GetTexture
//   B           a BSResource::Location over memory registered now (FileProbeLocation.h), then the same requests under
//               meshes\ and textures\heartofmagic\blackflame\vfsprobe\
//   C           the written files put into the engine's index with RegisterLoose (FileProbeEngine.h), then the requests
//   D           the engine's own LooseFileLocation rooted at <SKSE log folder>\HeartOfMagic_BlackFlame\fileprobe-root\
//               registered (FileProbeTestD.cpp); a frame later its files asked for under ...\heartofmagic\blackflame\droot\
//   Judge       once every asked texture has its GPU texture (NiSourceTexture::rendererTexture), kTextureWait at most:
//               every texture by its loaded size against the source DDS's (a miss may return a default texture, so "a
//               texture came back" is not enough)
// The run's first Demand is the source NIF itself (control-mesh): a FAIL there means the environment, not a route.
// Ends with one line "BlackFlame: file visibility probe: A-mesh PASS|FAIL, ..." and writes every line to
// <SKSE log folder>\HeartOfMagic_BlackFlame_fileprobe.txt. With devAutoQuit (and selfTest off) the game then quits through
// DevAutomation (10 s later); kGiveUp after the plugin loads without an end, the file says TIMEOUT and it quits anyway.
// Every engine call is SEH-guarded (GuardedKeep); a step that faults or throws is logged and the run goes on.

namespace BlackFlame::FileVisibilityProbe
{
    inline constexpr auto kMenuSettle = std::chrono::seconds(8);
    inline constexpr auto kTextureWait = std::chrono::seconds(5);
    inline constexpr auto kRunDeadline = std::chrono::seconds(90);  // from the first step: the rest is SKIPPED
    inline constexpr auto kGiveUp = std::chrono::minutes(5);        // from kDataLoaded, with devAutoQuit only
    inline constexpr const char* kResultFile = "HeartOfMagic_BlackFlame_fileprobe.txt";

    /// kDataLoaded: when devFileVisibilityProbe is on, watches for the main menu (and the give-up timer with devAutoQuit)
    void Install();
}
