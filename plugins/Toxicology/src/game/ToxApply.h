#pragma once

// =============================================================================
// Toxicology - putting the copied poison on a target, then an immunity window
// =============================================================================
// Spec 5.4. Per (target FormID, poison FormID): a hit casts the poison unless
// the pair is still immune. A cast (a caster was found, whether or not a
// lasting effect showed up) records the time and D, the longest actual
// duration of the poison's new effects on the target (perks included; the
// poison record's longest duration when none is seen, 0 for an instant-only
// poison). The pair is immune until cast time + D + max(D x immunityMultiplier,
// minImmunitySeconds) (Toxicology::ReapplyAt); a hit before that does nothing.
// The time is game time (game/ToxApply.cpp, "Clock"), so it does not run while
// a menu pauses the game. The records are never saved: a save load, a new
// game and a co-save Revert empty them.
//
// Threads: game main thread only - the hit task (ToxHits.cpp), the self test
// and the SKSE messaging listener / co-save Revert callback
// (ClearApplications). No lock; a caller on another thread would need one first.
// =============================================================================

#include <optional>

namespace RE
{
    class Actor;
    class ActiveEffect;
    class AlchemyItem;
    template <class T> class BSSimpleList;
}

namespace ToxGame
{
    // kNoCaster stays last: ToxHits.h counts the kinds as kNoCaster + 1
    enum class ApplyOutcome
    {
        kImmune,          // the pair is inside its immunity window: nothing done
        kCast,            // cast, and a new effect of the poison showed up on the target
        kCastNoEffect,    // cast (and recorded), but no new effect of the poison showed up on the target
        kNoCaster,        // the caster's instant MagicCaster was missing: not cast, not recorded
    };

    // Spec 5.4 for one hit. a_target, a_poison and a_player are non-null; the caller checked the
    // ability, the setting, that the target is alive and not the player, and that the poison is
    // the current choice.
    ApplyOutcome ApplyPoison(RE::Actor* a_target, RE::AlchemyItem* a_poison, RE::Actor* a_player);

    // A recorded pair's window, in seconds at the current timescale
    struct ImmunityWindow
    {
        double durationSeconds = 0.0;   // D
        double immunitySeconds = 0.0;   // max(D x immunityMultiplier, minImmunitySeconds)
        double reapplyInSeconds = 0.0;  // from now until the next hit casts again (<= 0: it will)
    };

    // The pair's window, or nullopt when nothing is recorded for it or the game clock is unavailable.
    std::optional<ImmunityWindow> ImmunityOf(RE::FormID a_target, RE::FormID a_poison);

    // The clock the windows are measured on, in seconds (game time / timescale), or nullopt when the
    // calendar or a positive timescale is missing. For the self test's clock check.
    std::optional<double> GameClockSeconds();

    // Forgets every recorded pair. Called on kPreLoadGame, kNewGame and co-save Revert.
    void ClearApplications();

    // The actor's active effect list, or null. Always through Actor::AsMagicTarget(): the MagicTarget
    // base sits at 0x98 before 1.6.629 and 0xA0 from it (RE/A/Actor.h:777), so calling
    // Actor::GetActiveEffectList() directly uses the compile-time offset and, on the other runtime,
    // calls through the wrong vtable (crash 2026-10-17: RIP 0 in the apply step on 1.6.1170).
    RE::BSSimpleList<RE::ActiveEffect*>* ActiveEffectsOf(RE::Actor* a_actor);
}
