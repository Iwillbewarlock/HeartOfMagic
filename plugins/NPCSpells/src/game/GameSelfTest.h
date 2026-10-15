#pragma once

// =============================================================================
// NPC SPELLS - DEV-ONLY IN-GAME SELF TEST
// =============================================================================
// Runs only when Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json exists
// (the file never ships; the code is in the DLL, idle without it). Once per
// session, after the first save load, it spawns each sample (an NPC or a
// leveled NPC) next to the player, waits for its 3D and high process, reads
// its leveled spell cache and asks Actor::HasSpell for the spells only our
// lists give, logs SELFTEST lines, appends them to the report and deletes the
// spawned actors. Internals: GameSelfTestState.h, GameSelfTestCheck.cpp.
// =============================================================================

namespace NpcSpellsGame
{
    inline constexpr const char* kSelfTestPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json";

    // kPostLoadGame. Without the file: one file-exists check, nothing else. Starts the test at most once per session
    // (not again after a load ended it).
    void MaybeStartSelfTest();

    // kPreLoadGame: before the spawn the wait starts again; after it the next tick cleans up, fails the open samples
    // ("another save was loaded during the test") and ends the test. Nothing without a running test.
    void OnSelfTestPreLoad();

    // kSaveGame: a save while spawned actors stand in the world logs "SELFTEST WARN save taken while sample actors exist"
    void OnSelfTestSave();
}
