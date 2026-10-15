#pragma once

// =============================================================================
// NPC SPELLS - DEV-ONLY IN-GAME SELF TEST
// =============================================================================
// Runs only when Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json exists
// (the file never ships; the code is in the DLL, idle without it). Once per
// session, after the first save load, it spawns each sample (an NPC or a
// leveled NPC) next to the player, waits for its 3D and high process, walks
// its spells (Actor::VisitSpells builds the leveled spell cache), reads that
// cache and asks Actor::HasSpell for the spells only our
// lists give, logs SELFTEST lines, appends them to the report and deletes the
// spawned actors. Internals: GameSelfTestState.h, GameSelfTestCheck.cpp.
// =============================================================================

namespace NpcSpellsGame
{
    inline constexpr const char* kSelfTestPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json";

    // kPostLoadGame. Without the file: one file-exists check, nothing else. Starts the test at most once per session: a
    // test that ended before its spawn (no player 3D in time, a new game, an error) logs SELFTEST RESTART, not DONE, and
    // lets the next load start it again; one that spawned ends with SELFTEST DONE and does not run again.
    void MaybeStartSelfTest();

    // kPreLoadGame: before the spawn the wait starts again; after it the next tick fails the open samples ("another save
    // was loaded during the test") and ends the test without touching the spawned refs (the load discards them).
    // Nothing without a running test.
    void OnSelfTestPreLoad();

    // kNewGame: the next tick ends a running test ("a new game was started during the test") with no spawn, and without
    // touching refs spawned before it
    void OnSelfTestNewGame();

    // kSaveGame: a save while spawned actors stand in the world logs "SELFTEST WARN save taken while sample actors exist"
    void OnSelfTestSave();
}
