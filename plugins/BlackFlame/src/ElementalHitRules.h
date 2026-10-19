#pragma once

#include <algorithm>
#include <cstdint>

// =============================================================================
// ELEMENTAL HIT RULES - the decisions of the last elemental hit record and the corpse mark, with no lock and no engine
// type (C++17: tools/blackflame-cosave-check tests them, ElementalHitRulesTests.h)
// =============================================================================
// HitPathAttribution.cpp RecordElementalHit takes the target's slot under its lock, asks DecideRecord, and on kWrite
// ApplyRecord; CorpseMark.cpp asks DecideMark and words the reason; ExtendWindow and WindowOpen are the window's two
// halves (the corpse mark's extension, AnyFireHitOpen's test). RE::FormID is std::uint32_t.

namespace BlackFlame::HitPath::Detail
{
    /// Who made an elemental hit (NoteFireEffect, the magic effect apply sink): the player's fire effect of a fire spell
    /// while stoked (the swap's own condition; fire only), any other elemental effect the player cast, or a non-player Actor
    /// caster other than the target (no caster, the target itself or a non-Actor caster: not recorded at all, 2026-10-20)
    enum class FireSource : std::uint8_t
    {
        kNone,
        kPlayerStoked,
        kPlayerPlain,
        kNpc
    };

    /// "player stoked" / "player not stoked" / "NPC" / "none"
    [[nodiscard]] inline const char* FireSourceName(FireSource a_source) noexcept
    {
        switch (a_source) {
        case FireSource::kPlayerStoked:
            return "player stoked";
        case FireSource::kPlayerPlain:
            return "player not stoked";
        case FireSource::kNpc:
            return "NPC";
        default:
            return "none";
        }
    }

    /// The element of a damage effect (MagicApplySink.h ElementOf: the resist value or the vanilla MagicDamage* keyword)
    enum class Element : std::uint8_t
    {
        kNone,
        kFire,
        kFrost,
        kShock
    };

    /// "fire" / "frost" / "shock" / "none"
    [[nodiscard]] inline const char* ElementName(Element a_element) noexcept
    {
        switch (a_element) {
        case Element::kFire:
            return "fire";
        case Element::kFrost:
            return "frost";
        case Element::kShock:
            return "shock";
        default:
            return "none";
        }
    }

    /// Which path recorded a hit: the ActiveEffect's hit shader start (HitEffectHook's 0x0D thunk) or the engine's
    /// TESMagicEffectApplyEvent (MagicApplySink.h, dispatched before the Papyrus OnMagicEffectApply handlers run)
    enum class HitOrigin : std::uint8_t
    {
        kEffectStart,
        kApplyEvent
    };

    /// The last elemental (fire, frost or shock) hit on one target, any caster: steady clock ticks (0: none) and GameClock
    /// seconds. The corpse mark asks that this last hit be the player's stoked fire (design v2 round 1: the killing blow's
    /// element)
    struct FireHit
    {
        std::uint32_t target = 0;               // RE::FormID
        FireSource source = FireSource::kNone;  // the last elemental hit's
        Element element = Element::kNone;       // the last elemental hit's
        HitOrigin origin = HitOrigin::kEffectStart;
        std::uint32_t mgef = 0;                 // the last elemental hit's MGEF (RE::FormID)
        std::int64_t lastAt = 0;                // the last elemental hit, any source (0: an empty slot)
        double lastGameAt = 0.0;                // GameClock::Seconds() at that hit
        std::int64_t lastStokedAt = 0;          // the last player-stoked hit (0: none)
        double markedUntilGame = 0.0;           // a corpse mark kept its window open until then (0: never marked)
    };

    /// One new elemental hit as RecordElementalHit gets it
    struct ElementalHit
    {
        std::uint32_t target = 0;
        Element element = Element::kNone;
        FireSource source = FireSource::kNone;
        bool targetDown = false;  // dying or dead
        HitOrigin origin = HitOrigin::kEffectStart;
        std::uint32_t mgef = 0;
    };

    enum class RecordAction : std::uint8_t
    {
        kIgnore,     // no target or no element: nothing written, not a kept record either
        kWrite,      // the slot takes the hit (a new slot first when the target has none)
        kKeepStoked  // the down target's player-stoked record stays against the player's later fire, not stoked
    };

    struct RecordDecision
    {
        RecordAction action = RecordAction::kIgnore;
        bool rejudge = false;            // the effect start of the MGEF whose apply event made the record: never kept
        bool keepOriginAndMgef = false;  // the apply event's proxy "stoked" on a corpse already recorded as stoked
    };

    /// a_record: the target's slot, nullptr when it has none. The killing blow is decided by then: the player's own later
    /// fire (Flame Cloak, an enchanted weapon) on a down target recorded as stoked does not undo the stoked kill; NPC fire
    /// and the player's frost or shock still overwrite. The effect start of the MGEF the apply event recorded re-judges that
    /// same effect with its spell (the apply event's stoked test is a proxy: a scroll, staff or enchantment sharing a fire
    /// spell's MGEF passes it), so it is not kept. The apply event's proxy "stoked" on a down target already recorded as
    /// stoked adds nothing to judge: the record keeps its own origin and MGEF, so a scroll's effect start on that corpse
    /// cannot re-judge it plain
    [[nodiscard]] inline RecordDecision DecideRecord(const FireHit* a_record, const ElementalHit& a_hit) noexcept
    {
        RecordDecision decision;
        if (a_hit.target == 0 || a_hit.element == Element::kNone) {
            return decision;
        }
        decision.rejudge = a_record && a_hit.origin == HitOrigin::kEffectStart && a_record->origin == HitOrigin::kApplyEvent &&
                           a_record->mgef == a_hit.mgef;
        const bool keep = a_record && !decision.rejudge && a_hit.targetDown && a_record->source == FireSource::kPlayerStoked &&
                          a_hit.source == FireSource::kPlayerPlain && a_hit.element == Element::kFire;
        if (keep) {
            decision.action = RecordAction::kKeepStoked;
            return decision;
        }
        decision.action = RecordAction::kWrite;
        decision.keepOriginAndMgef = a_record && a_hit.origin == HitOrigin::kApplyEvent && a_hit.targetDown &&
                                     a_record->source == FireSource::kPlayerStoked && a_hit.source == FireSource::kPlayerStoked;
        return decision;
    }

    /// kWrite only (anything else leaves a_slot as it is): a_slot (its target already set) takes the hit. markedUntilGame
    /// is not touched: only a slot taken for another target clears it
    inline void ApplyRecord(FireHit& a_slot, const ElementalHit& a_hit, const RecordDecision& a_decision, std::int64_t a_steadyNow,
        double a_gameNow) noexcept
    {
        if (a_decision.action != RecordAction::kWrite) {
            return;
        }
        if (!a_decision.keepOriginAndMgef) {
            a_slot.origin = a_hit.origin;
            a_slot.mgef = a_hit.mgef;
        }
        a_slot.source = a_hit.source;
        a_slot.element = a_hit.element;
        a_slot.lastAt = a_steadyNow;
        a_slot.lastGameAt = a_gameNow;
        if (a_hit.source == FireSource::kPlayerStoked) {
            a_slot.lastStokedAt = a_steadyNow;
        }
    }

    /// The window of an occupied slot (lastAt != 0, the caller's test) is open: its last hit at most a_window game seconds
    /// before a_now, or a corpse mark keeps it open until markedUntilGame (both inclusive)
    [[nodiscard]] inline bool WindowOpen(const FireHit& a_hit, double a_now, double a_window) noexcept
    {
        return a_now - a_hit.lastGameAt <= a_window || a_now <= a_hit.markedUntilGame;
    }

    /// A corpse mark keeps the window open until a_untilGame; never shortens an earlier mark's
    inline void ExtendWindow(FireHit& a_hit, double a_untilGame) noexcept
    {
        a_hit.markedUntilGame = (std::max)(a_hit.markedUntilGame, a_untilGame);
    }

    enum class MarkReason : std::uint8_t
    {
        kMarked,          // the last elemental hit is the player's stoked fire, at most the window old
        kMarkedKeptOpen,  // the same, older than the window, but an earlier mark keeps it open
        kNoGameClock,     // GameClock is not advancing (the main-thread queue has not run two frames)
        kNoHit,           // no elemental hit remembered on the target
        kNotStokedFire,   // the last elemental hit is not the player's stoked fire (NPC fire, plain fire, frost, shock)
        kOverWindow       // the player's stoked fire, older than the window, no mark keeps it open
    };

    struct MarkDecision
    {
        MarkReason reason = MarkReason::kNoHit;
        double age = 0.0;  // a_now - lastGameAt (0 with no clock or no hit)

        [[nodiscard]] bool Marked() const noexcept { return reason == MarkReason::kMarked || reason == MarkReason::kMarkedKeptOpen; }
    };

    /// The corpse mark's decision for a dying or dead target the player killed (CorpseMark.cpp Judge checks that), in this
    /// order: the clock, a remembered hit, the killing blow's element (a stoke, then a shock kill: FEC's shock disintegrate is
    /// not marked), the window, an earlier mark's extension
    [[nodiscard]] inline MarkDecision DecideMark(const FireHit* a_hit, bool a_clockAdvancing, double a_now, double a_window) noexcept
    {
        MarkDecision decision;
        if (!a_clockAdvancing) {
            decision.reason = MarkReason::kNoGameClock;
            return decision;
        }
        if (!a_hit) {
            decision.reason = MarkReason::kNoHit;
            return decision;
        }
        decision.age = a_now - a_hit->lastGameAt;
        if (a_hit->source != FireSource::kPlayerStoked || a_hit->element != Element::kFire) {
            decision.reason = MarkReason::kNotStokedFire;
        } else if (decision.age <= a_window) {
            decision.reason = MarkReason::kMarked;
        } else if (a_now <= a_hit->markedUntilGame) {
            decision.reason = MarkReason::kMarkedKeptOpen;
        } else {
            decision.reason = MarkReason::kOverWindow;
        }
        return decision;
    }
}
