#pragma once

// =============================================================================
// SCAN AT GAME START (docs/ARCHITECTURE.md, "Scan at game start")
// =============================================================================
//
// When the load order differs from the one the catalog was made from
// (ScanRecord.h), the first main menu of a session scans every spell by
// itself: the form reads in steps on the game thread, one step a frame, then
// the text, the catalog (spell_catalog.json) and spell_scan_output.json on a
// worker. It never touches the tree (spell_tree.json and its record), the
// learning progress, the panel or the game's forms: the perk adapters take the
// new catalog at the next game start, Spell Research at the next save load
// (or right after the scan when a save was loaded meanwhile).
//
// It does not run when the setting "Build Spell Tree at Start" is Off, when
// config.json cannot be read, when there is no tree with spells (the panel's
// build at game start scans then), or while another scan runs (tried again at
// the next main menu). A save load or a new game during the form reads drops
// them; the next main menu tries again.
//
// selftest.json { "autoScan": { "enabled": true, ... } } (dev installs) runs it
// whatever the record says and logs PASS/FAIL lines: the run, the tree files
// unchanged, the longest frame against the main menu's own, the steps against
// one CollectAllSpells, a dropped run.

namespace AutoScan
{
    // kDataLoaded: watch for the main menu
    void Install();

    // kPreLoadGame and kNewGame (game thread): form reads in progress are dropped
    void OnGameChanging() noexcept;
}
