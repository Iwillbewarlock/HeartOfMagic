#pragma once

// =============================================================================
// TestSpawn - hostile mages to practise the counter on (settings page, Test)
// =============================================================================
// No plugin file: vanilla leveled warlocks from Skyrim.esm are placed about
// kSpawnDistance in front of the player; they are hostile by faction and pick
// the fight themselves. Fire and Frost cast missiles (counterable), Shock casts
// a beam (not counterable - a check that no prompt shows). "Remove" disables and
// deletes every mage placed this session. Game thread only.
// =============================================================================

namespace SLCombat
{
    enum class TestMage
    {
        kFire,
        kFrost,
        kShock
    };

    class TestSpawn
    {
    public:
        static void Spawn(TestMage a_kind);
        static void RemoveAll();
        static std::size_t Count();
    };
}
