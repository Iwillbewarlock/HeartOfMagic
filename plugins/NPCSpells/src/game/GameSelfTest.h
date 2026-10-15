#pragma once

// =============================================================================
// NPC SPELLS - DEV-ONLY IN-GAME SELF TEST
// =============================================================================
// Runs only when Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json exists
// (never shipped). Once per session, after a save load or a new game, it spawns
// each sample NPC next to the player, waits for its 3D and high process, asks
// Actor::HasSpell for the spells only our lists give, logs SELFTEST lines,
// appends them to the report and deletes the spawned actors.
// =============================================================================

namespace NpcSpellsGame
{
    inline constexpr const char* kSelfTestPath = "Data/SKSE/Plugins/HeartOfMagic_NPCSpells_selftest.json";

    // kPostLoadGame / kNewGame. Without the file: one file-exists check, nothing else. Starts the test at most once per session.
    void MaybeStartSelfTest();
}
