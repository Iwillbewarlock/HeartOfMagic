#pragma once

// =============================================================================
// CombatConfig - SL_Combat settings (Data/SKSE/Plugins/SL_Combat.json)
// =============================================================================
// Every value has a default here; the JSON file only needs the keys a player
// wants to change. Read once at kDataLoaded. See docs/COMBAT.md for each key.
// =============================================================================

#include <string>

namespace SLCombat
{
    /// How a countered projectile stops colliding with the player.
    enum class FilterMode
    {
        kShift,        ///< filter = (filter & 0xFFFF) | (playerFilter << 16), as Elden Parry does
        kSystemGroup   ///< copy the player's collision system group onto the projectile
    };

    /// What a counter does with the incoming spell.
    enum class CounterResult
    {
        kWard,     ///< raise a ward now; the game's ward absorbs the hit
        kAbsorb,   ///< drop the hit and remove the missile (no explosion)
        kReflect   ///< send the missile back at its caster
    };

    struct CombatConfig
    {
        // --- counter -------------------------------------------------------
        bool  enableCounter = true;
        int   counterKey = -1;           ///< the counter key (CounterKey.h); -1 = the game's block key
        float counterWindow = 0.25f;     ///< seconds before impact a counter press still counts
        float wardBonusWindow = 0.10f;   ///< extra seconds when either hand holds a ward
        float frontAngle = 60.0f;        ///< degrees either side of the player's facing
        bool  skipIfOtherParryMod = true;
        FilterMode filterMode = FilterMode::kShift;
        bool  hookReturnOnCounter = false;  ///< value the collision hook returns when it countered
        bool  requireKnownWard = true;   ///< the counter needs at least one known ward spell
        bool  requireSpellInHand = true; ///< the counter needs a spell in at least one hand
        /// With spells in both hands and this perk, the counter's ward is dual-cast
        std::string dualCastPerk = "Skyrim.esm|0x153D1";  ///< Restoration Dual Casting
        CounterResult counterResult = CounterResult::kWard;

        // --- ward ----------------------------------------------------------
        float wardLingerSeconds = 1.0f;  ///< the counter's ward stays up this long even if let go (free)
        bool  holdWard = true;           ///< holding the counter key after a counter keeps a ward up
        float holdRefreshSeconds = 0.25f;  ///< how often the held ward is re-cast and charged
        bool  sphereWardAllAngles = true;  ///< with 360 Ward, counters from every direction (else frontAngle)

        // --- counter prompt (SkyPrompt) -------------------------------------
        float warnLeadSeconds = 0.6f;    ///< how early the prompt appears (seconds before impact)
        float warnRadius = 120.0f;       ///< a missile passing farther than this from the chest is ignored
        std::string promptText = "Counter";  ///< the prompt's label (SkyPrompt accepts $translation keys)

        // --- response ------------------------------------------------------
        float slowMoScale = 0.4f;        ///< time multiplier during slow motion (1 = off)
        float slowMoDuration = 0.35f;    ///< real seconds
        bool  freeNextCast = true;       ///< refund the magicka of the next cast after a counter
        float freeCastWindow = 3.0f;     ///< seconds the free cast stays available
        float xpPerCounter = 5.0f;       ///< learning XP to the spell cast right after a counter
        std::string hudText = "Countered";  ///< corner message "<hudText>: <spell>"; empty = none

        // --- test ----------------------------------------------------------
        bool  testTools = false;         ///< the settings page shows the hostile-mage spawner

        static CombatConfig& Get();
        void Load();

        /// Writes every key to Data/SKSE/Plugins/SL_Combat.json (the settings page). False on failure.
        bool Save() const;
    };
}
