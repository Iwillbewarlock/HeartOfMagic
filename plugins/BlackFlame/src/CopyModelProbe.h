#pragma once

#include "Common.h"

// =============================================================================
// COPY MODEL PROBE - does the game load every copy NIF? (v2i, causes B: Flame Cloak, Dragonfire, Incinerate)
// =============================================================================
// Modes (config copyModelProbe): "off"; "log" (default) - probe and log, never take a copy out (the in-game run of 46855dd
// reported 347 of 356 copy models failed with error 1 although they load in play, so the probe's way of asking is in doubt);
// "drop" - the safety net below acts. Every failure line gives the exact Demand string of the copy and of its source (the
// control) side by side.
// Cached: a complete probe's result is written to <SKSE log folder>\HeartOfMagic_BlackFlame\HeartOfMagic_BlackFlame_probe.json,
// keyed by the copies map's bytes, its write time and the plugin version; a later start with the same key applies it at
// kDataLoaded (applied only in "drop") and probes nothing. Otherwise (no cache, another map, selfTest / testerMode,
// copyModelProbeAlways), after
// kDataLoaded, on the main thread in 8 ms slices over the drains (the main menu, and on into play if a save is loaded first;
// a 20 s budget of probing in all): BSModelDB::Demand for every
// distinct model path of the filled copies - the record's model of each PROJ, EXPL, IPCT, HAZD and ARTO copy and a PROJ
// copy's muzzle flash model - as the engine's own LoadGraphics asks for it (path as the record stores it, default
// ArgsType = RequestModel2's {0, 3, 0x1010001}). A summary (ok, failed, ms), every failure with its ErrorCode, and the
// paths left out when the time budget runs out. Safety net: a copy whose path failed while its SOURCE's same model path
// loaded (the control: a probe that fails for everything takes nothing out; a copy that kept the source's path, or whose
// control is past the budget, is kept) is taken out of CopyOf for the session (FormCopies.h DisableCopiesForSession), so
// that form stays original instead of invisible; a failed grey muzzle only puts the source's muzzle back. Then
// RefreshCopies points other copies' links to a dropped copy back to its source.

namespace BlackFlame
{
    /// kDataLoaded, after the copies are filled: applies the cached result for this copies map, or queues the probe for the
    /// main thread
    void QueueCopyModelProbe();

    /// "off", "from the cache (...)", "probing", "probed this start (...)" (any thread; the tester file)
    [[nodiscard]] std::string CopyModelProbeStatus();
}
