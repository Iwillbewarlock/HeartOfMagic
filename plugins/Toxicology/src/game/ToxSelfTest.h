#pragma once

// =============================================================================
// Toxicology - dev-only in-game self test
// =============================================================================
// Spec 9. Runs only when Data/SKSE/Plugins/HeartOfMagic_Toxicology_selftest.json
// exists (the file never ships; the code is in the DLL, idle without it). Once
// per session, startDelayMs (default 30 s) after the player stands loaded in a
// save, it checks the tome (teaches the power, value 350) and its reward:
// the save's ability and tomes are taken, Few and Far Between is set to stage
// 200 through Papyrus (or, already there, the load path runs) and the tome must
// be given once and only once; the ability and tomes are put back. Step 4b (1.1)
// checks the power's records, casts the power twice through the player's
// instant caster (the ability must flip each time, one toggle per cast) and runs
// the 1.0 save migration; the ability and the power are put back. Then it gives
// the ability and two poisons, spawns targets in front of the
// player, casts the load order's poison spells at them (one per delivery) and
// checks what the addon did; then the bottles and the saved Magicka count.
// Every result is a log line:
//   SELFTEST PASS <step>: <detail>     SELFTEST FAIL <step>: <detail>
//   SELFTEST INFO <what>: <detail>     SELFTEST DONE pass=N fail=M ...
// What it added (ability, poisons, targets, the cloak on the player) is taken
// back before DONE. The quest's stage 200 (with its fragment's key, thane count
// and relationship) and the reward flag stay: the save is changed for good, so
// use a disposable save. The Magicka count keeps a test value until the reload
// check: the log asks for a save and a load of that save, and the next load
// checks the count came back, and the reward flag and stage 200 when the tome
// was given (HeartOfMagic_Toxicology_selftest_state.json carries the expected
// value, the count from before the test, which is put back after the check,
// and whether to check the reward). Internals: game/ToxSelfTestState.h.
// =============================================================================

#include <cstdint>

namespace ToxGame
{
    inline constexpr const char* kSelfTestPath = "Data/SKSE/Plugins/HeartOfMagic_Toxicology_selftest.json";
    inline constexpr const char* kSelfTestStatePath = "Data/SKSE/Plugins/HeartOfMagic_Toxicology_selftest_state.json";

    // kPostLoadGame (after the poison set and the selection are refreshed). Without the dev file:
    // one file-exists check, nothing else. With it: a pending reload check (the state file) runs
    // at once and the main test does not start this session; otherwise the main test starts once
    // per session: one that ended before it gave the player anything logs SELFTEST RESTART and may
    // start again at a later load.
    void MaybeStartSelfTest();

    // kPreLoadGame (a_saveName: the message's data, may be empty). Before the test gave anything the
    // wait starts again; after, the next tick ends it without touching the old world. With the dev
    // file, notes the save name for the reload check.
    void OnSelfTestPreLoad(const char* a_saveName, std::uint32_t a_length);

    // kNewGame: a running test ends at its next tick, the old world untouched
    void OnSelfTestNewGame();

    // kSaveGame (a_saveName: the message's data, may be empty). Warns about a save taken while the
    // test's changes are in the world; with the dev file, notes the save the reload check waits for.
    void OnSelfTestSave(const char* a_saveName, std::uint32_t a_length);
}
