#pragma once

// =============================================================================
// Toxicology - the lesser power that turns the feature on and off (1.1)
// =============================================================================
// Spec 2026-10-20. On = the player has the ability (ToxData.h PlayerHasAbility);
// every gate of 1.0 (`enabled && PlayerHasAbility()`) follows it, and the engine
// keeps the ability in the game's save, so there is no co-save record for it.
// The power (HoMTox_ToxicologyPower, its effect HoMTox_TogglePowerEffect) is
// seen two ways, each enough on its own:
//   - TESSpellCastEvent: the spell is the power and the caster is the player;
//   - TESMagicEffectApplyEvent: the effect is the power's and both its caster and
//     its target are the player.
// Either queues a toggle task (main thread) carrying the session generation
// (ToxHits.h), dropped after a load or a new game. One cast may raise both
// events: a toggle less than kRepeatToggleSeconds (steady clock) after the last
// accepted one is dropped as a repeat (core IsRepeatToggle); the clock is
// forgotten at every load and new game. The toggle removes the ability when it
// is held and adds it otherwise (core DecideToggle), reads HasSpell again and
// logs and announces only the state reached (MESG 0x806 / 0x807's name as the
// HUD notification). Then OnSelectionMaybeChanged (ToxSelection.h) applies the
// new state at once: the caster hook's flag, and the icon keyword off the
// copied poison when off, back on when on. Neither path looks at "enabled" or
// the ability: the power works while the feature is off. With "enabled" off
// the ability is still added and removed (the log says the feature stays off).
//
// A 1.0 save knows the ability but not the power: after a successful load a
// task gives it the power (core DecideMigration), so it stays on.
//
// Threads: the sinks may run on any thread; they compare FormIDs with atomics
// (written at kDataLoaded, before the sinks exist), count the event for the
// self test (atomics) and queue the task. Everything else runs on the main
// thread.
// =============================================================================

#include "core/ToxCore.h"

#include <cstdint>

namespace ToxGame
{
    // In game (2026-10-20, AE 1.6.1170, a real Z cast) the two events of one cast came 3 ms apart (apply event
    // first). The power's charge time is 0.5 s (ESL.md), so two real uses are at least 0.5 s apart: a window
    // well under that keeps a quick second use from being dropped (1.0 s could swallow one)
    inline constexpr double kRepeatToggleSeconds = 0.25;

    // Which event queued a toggle
    enum class PowerPath
    {
        kSpellCast,    // TESSpellCastEvent
        kEffectApply,  // TESMagicEffectApplyEvent
    };

    const char* PowerPathName(PowerPath a_path);

    // What the sinks and the toggle task have seen since the DLL loaded (any thread; for the self test)
    struct PowerTrace
    {
        std::uint32_t castEvents = 0;   // the player's TESSpellCastEvents for the power
        std::uint32_t applyEvents = 0;  // TESMagicEffectApplyEvents of the power's effect, the player on both sides
        std::uint32_t toggles = 0;      // toggle tasks that changed the ability
        std::uint32_t repeats = 0;      // toggle tasks dropped as a repeat
        std::uint32_t failures = 0;     // toggle tasks that found no player or ability, or left the ability unchanged
    };

    PowerTrace PowerTraceNow();

    // Registers both sinks. Call once at kDataLoaded, on the main thread, after LoadForms.
    void RegisterPowerSinks();

    // kPreLoadGame and kNewGame: the next toggle is never a repeat
    void ResetPowerToggle();

    // Main thread. The toggle task's body: debounce, then the ability added or removed. Returns true when
    // the ability changed.
    bool TogglePower(PowerPath a_path);

    // kPostLoadGame after a successful load only: queues a task that calls MigratePower.
    void QueuePowerMigration();

    // Main thread. Reads the ability and the power, decides (core DecideMigration), logs and, for
    // kGrantPower, adds the power (the result is read back and logged). Returns the decision.
    Toxicology::MigrationAction MigratePower();
}
