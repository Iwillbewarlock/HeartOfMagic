#pragma once

// =============================================================================
// ELEMENTAL HIT RULES TESTS - plugins/BlackFlame/src/ElementalHitRules.h: the last elemental hit record (keep, re-judge,
// the apply event's proxy on a stoked corpse, replace) and the corpse mark (killing blow's element, window, extension)
// =============================================================================
// Each case drives one target's slot the way HitPathAttribution.cpp RecordElementalHit does (DecideRecord, a new slot
// when there is none, ApplyRecord) and then asks DecideMark as CorpseMark.cpp does. Game seconds are exact doubles.
// Included by main.cpp.

#include "../../plugins/BlackFlame/src/ElementalHitRules.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace ElementalHitRulesTests
{
    namespace H = BlackFlame::HitPath::Detail;

    constexpr std::uint32_t kTarget = 0x0001A2B3;
    constexpr std::uint32_t kFireboltMgef = 0x00012F03;   // a fire spell's MGEF (shared by a scroll in the scroll case)
    constexpr std::uint32_t kFlameCloakMgef = 0x0003AE9F;
    constexpr std::uint32_t kShockMgef = 0x00013CAB;
    constexpr double kWindow = 5.0;

    // One target's slot as the FireHits table holds it
    struct Target
    {
        bool has = false;
        H::FireHit slot;
        std::int64_t ticks = 0;  // a steady clock stand-in: each record call is one tick later

        H::RecordDecision Record(const H::ElementalHit& a_hit, double a_game)
        {
            const auto decision = H::DecideRecord(has ? &slot : nullptr, a_hit);
            if (decision.action == H::RecordAction::kWrite) {
                if (!has) {
                    slot = H::FireHit{ a_hit.target };
                    has = true;
                }
                H::ApplyRecord(slot, a_hit, decision, ++ticks, a_game);
            }
            return decision;
        }

        [[nodiscard]] H::MarkDecision Mark(double a_now, bool a_clock = true) const
        {
            return H::DecideMark(has ? &slot : nullptr, a_clock, a_now, kWindow);
        }
    };

    inline H::ElementalHit Hit(H::Element a_element, H::FireSource a_source, bool a_down, H::HitOrigin a_origin, std::uint32_t a_mgef)
    {
        H::ElementalHit hit;
        hit.target = kTarget;
        hit.element = a_element;
        hit.source = a_source;
        hit.targetDown = a_down;
        hit.origin = a_origin;
        hit.mgef = a_mgef;
        return hit;
    }

    inline H::ElementalHit StokedFire(bool a_down, H::HitOrigin a_origin = H::HitOrigin::kEffectStart, std::uint32_t a_mgef = kFireboltMgef)
    {
        return Hit(H::Element::kFire, H::FireSource::kPlayerStoked, a_down, a_origin, a_mgef);
    }

    inline H::ElementalHit PlainFire(bool a_down, H::HitOrigin a_origin, std::uint32_t a_mgef)
    {
        return Hit(H::Element::kFire, H::FireSource::kPlayerPlain, a_down, a_origin, a_mgef);
    }

    inline std::string ReasonName(H::MarkReason a_reason)
    {
        switch (a_reason) {
        case H::MarkReason::kMarked:
            return "marked";
        case H::MarkReason::kMarkedKeptOpen:
            return "marked, kept open";
        case H::MarkReason::kNoGameClock:
            return "no game clock";
        case H::MarkReason::kNoHit:
            return "no hit";
        case H::MarkReason::kNotStokedFire:
            return "not stoked fire";
        case H::MarkReason::kOverWindow:
            return "over the window";
        default:
            return "?";
        }
    }

    template <class Check>
    void RunRecord(Check&& a_check)
    {
        using A = H::RecordAction;
        using O = H::HitOrigin;
        {
            Target t;  // a stoked kill, then Flame Cloak (the player's fire, not stoked) on the corpse
            const auto kill = t.Record(StokedFire(false), 100.0);
            const auto cloak = t.Record(PlainFire(true, O::kEffectStart, kFlameCloakMgef), 102.0);
            const auto cloakEvent = t.Record(PlainFire(true, O::kApplyEvent, kFlameCloakMgef), 102.5);
            const auto mark = t.Mark(103.0);
            a_check(kill.action == A::kWrite && cloak.action == A::kKeepStoked && !cloak.rejudge && cloakEvent.action == A::kKeepStoked,
                "elemental hit: a stoked kill, then Flame Cloak on the corpse (effect start and apply event): the stoked record is kept");
            a_check(t.slot.source == H::FireSource::kPlayerStoked && t.slot.mgef == kFireboltMgef && t.slot.lastGameAt == 100.0 &&
                        t.slot.lastAt == 1,
                "elemental hit: the kept record is the kill's unchanged (source, MGEF, game time, ticks)");
            a_check(mark.reason == H::MarkReason::kMarked && mark.Marked() && mark.age == 3.0,
                "elemental hit: the corpse after Flame Cloak is marked, aged from the stoked kill (" + ReasonName(mark.reason) + ")");
        }
        {
            Target t;  // the same Flame Cloak on a living target recorded as stoked: not down, so it replaces
            (void)t.Record(StokedFire(false), 100.0);
            const auto cloak = t.Record(PlainFire(false, O::kEffectStart, kFlameCloakMgef), 101.0);
            a_check(cloak.action == A::kWrite && t.slot.source == H::FireSource::kPlayerPlain && t.Mark(101.5).reason == H::MarkReason::kNotStokedFire,
                "elemental hit: the player's plain fire on a living stoked target replaces it, not marked");
        }
        {
            Target t;  // a stoked hit, then an NPC's fire on the corpse
            (void)t.Record(StokedFire(false), 100.0);
            const auto npc = t.Record(Hit(H::Element::kFire, H::FireSource::kNpc, true, O::kEffectStart, kFireboltMgef), 101.0);
            const auto mark = t.Mark(101.5);
            a_check(npc.action == A::kWrite && t.slot.source == H::FireSource::kNpc && t.slot.lastGameAt == 101.0 &&
                        mark.reason == H::MarkReason::kNotStokedFire && !mark.Marked(),
                "elemental hit: a stoked hit, then NPC fire on the corpse: replaced, not marked (" + ReasonName(mark.reason) + ")");
        }
        {
            Target t;  // a stoke, then the player's shock kills (FEC's shock disintegrate)
            (void)t.Record(StokedFire(false), 100.0);
            const auto shock = t.Record(Hit(H::Element::kShock, H::FireSource::kPlayerPlain, true, O::kEffectStart, kShockMgef), 101.0);
            const auto mark = t.Mark(101.5);
            a_check(shock.action == A::kWrite && t.slot.element == H::Element::kShock && t.slot.lastStokedAt == 1 &&
                        mark.reason == H::MarkReason::kNotStokedFire,
                "elemental hit: a stoke, then a shock kill: replaced (shock, the stoked tick stays), not marked (" +
                    ReasonName(mark.reason) + ")");
            Target frost;
            (void)frost.Record(StokedFire(false), 100.0);
            const auto cold = frost.Record(Hit(H::Element::kFrost, H::FireSource::kPlayerPlain, true, O::kApplyEvent, kShockMgef), 101.0);
            a_check(cold.action == A::kWrite && frost.Mark(101.5).reason == H::MarkReason::kNotStokedFire,
                "elemental hit: a stoke, then the player's frost on the corpse: replaced, not marked");
        }
        {
            Target t;  // a scroll sharing a fire spell's MGEF: the apply event's proxy says stoked, the effect start knows better
            const auto proxy = t.Record(StokedFire(false, O::kApplyEvent, kFireboltMgef), 100.0);
            const bool proxyStored = t.slot.origin == O::kApplyEvent && t.slot.source == H::FireSource::kPlayerStoked;
            const auto start = t.Record(PlainFire(true, O::kEffectStart, kFireboltMgef), 100.0);
            const auto mark = t.Mark(100.5);
            a_check(proxy.action == A::kWrite && proxyStored && start.rejudge && start.action == A::kWrite &&
                        t.slot.source == H::FireSource::kPlayerPlain && t.slot.origin == O::kEffectStart,
                "elemental hit: scroll, apply event proxy stoked then effect start plain, same MGEF, target down: re-judged plain");
            a_check(mark.reason == H::MarkReason::kNotStokedFire,
                "elemental hit: the re-judged scroll kill is not marked (" + ReasonName(mark.reason) + ")");
            Target other;  // the same record and a different MGEF's plain effect start on the corpse: no re-judge, kept
            (void)other.Record(StokedFire(false, O::kApplyEvent, kFireboltMgef), 100.0);
            const auto cloak = other.Record(PlainFire(true, O::kEffectStart, kFlameCloakMgef), 100.5);
            a_check(!cloak.rejudge && cloak.action == A::kKeepStoked,
                "elemental hit: an apply event's stoked record, then another MGEF's plain effect start on the corpse: kept");
        }
        {
            Target t;  // the apply event's proxy "stoked" on a corpse already recorded as stoked by an effect start
            (void)t.Record(StokedFire(false, O::kEffectStart, kFireboltMgef), 100.0);
            const auto proxy = t.Record(StokedFire(true, O::kApplyEvent, kFlameCloakMgef), 101.0);
            a_check(proxy.action == A::kWrite && proxy.keepOriginAndMgef && t.slot.origin == O::kEffectStart &&
                        t.slot.mgef == kFireboltMgef && t.slot.source == H::FireSource::kPlayerStoked && t.slot.lastGameAt == 101.0,
                "elemental hit: proxy stoked on a stoked corpse: the record keeps its origin and MGEF (its time moves)");
            const auto start = t.Record(PlainFire(true, O::kEffectStart, kFlameCloakMgef), 101.0);
            a_check(!start.rejudge && start.action == A::kKeepStoked && t.Mark(102.0).reason == H::MarkReason::kMarked,
                "elemental hit: that scroll's own plain effect start cannot re-judge the stoked corpse: kept, marked");
            Target alive;  // the proxy on a living stoked target is an ordinary write: its origin and MGEF are taken
            (void)alive.Record(StokedFire(false, O::kEffectStart, kFireboltMgef), 100.0);
            const auto living = alive.Record(StokedFire(false, O::kApplyEvent, kFlameCloakMgef), 101.0);
            a_check(living.action == A::kWrite && !living.keepOriginAndMgef && alive.slot.origin == O::kApplyEvent &&
                        alive.slot.mgef == kFlameCloakMgef,
                "elemental hit: proxy stoked on a living stoked target takes the apply event's origin and MGEF");
        }
        {
            Target t;  // no record yet: nothing to keep, nor a proxy to hold back
            const auto first = t.Record(PlainFire(true, O::kEffectStart, kFlameCloakMgef), 100.0);
            Target u;
            const auto proxy = u.Record(StokedFire(true, O::kApplyEvent, kFireboltMgef), 100.0);
            a_check(first.action == A::kWrite && t.slot.source == H::FireSource::kPlayerPlain && t.slot.target == kTarget &&
                        proxy.action == A::kWrite && !proxy.keepOriginAndMgef && u.slot.origin == O::kApplyEvent,
                "elemental hit: a down target with no record takes the first hit as it is (plain written, proxy origin taken)");
            H::ElementalHit none = PlainFire(true, O::kEffectStart, kFlameCloakMgef);
            none.target = 0;
            H::ElementalHit noElement = StokedFire(false);
            noElement.element = H::Element::kNone;
            a_check(H::DecideRecord(&t.slot, none).action == A::kIgnore && H::DecideRecord(&t.slot, noElement).action == A::kIgnore,
                "elemental hit: no target or no element is ignored");
        }
    }

    template <class Check>
    void RunMark(Check&& a_check)
    {
        const double after105 = std::nextafter(105.0, std::numeric_limits<double>::infinity());
        {
            Target t;
            (void)t.Record(StokedFire(false), 100.0);
            const auto edge = t.Mark(105.0);
            const auto past = t.Mark(after105);
            a_check(edge.reason == H::MarkReason::kMarked && edge.age == kWindow,
                "corpse mark: exactly the window (5 game s) after the stoked fire is inside (" + ReasonName(edge.reason) + ")");
            a_check(past.reason == H::MarkReason::kOverWindow && !past.Marked() && past.age > kWindow,
                "corpse mark: the next double after the window is outside (" + ReasonName(past.reason) + ")");
            a_check(H::WindowOpen(t.slot, 105.0, kWindow) && !H::WindowOpen(t.slot, after105, kWindow),
                "corpse mark: AnyFireHitOpen's window test has the same edge");
            a_check(t.Mark(101.0, false).reason == H::MarkReason::kNoGameClock && Target{}.Mark(101.0).reason == H::MarkReason::kNoHit,
                "corpse mark: no game clock and no remembered hit are not marked, the clock first");
        }
        {
            Target t;  // an FEC chain: the first Play marks at 103 and keeps the window open 5 more
            (void)t.Record(StokedFire(false), 100.0);
            const auto first = t.Mark(103.0);
            H::ExtendWindow(t.slot, 103.0 + kWindow);
            const auto later = t.Mark(107.0);
            const auto edge = t.Mark(108.0);
            const auto past = t.Mark(std::nextafter(108.0, std::numeric_limits<double>::infinity()));
            a_check(first.reason == H::MarkReason::kMarked && later.reason == H::MarkReason::kMarkedKeptOpen && later.Marked() &&
                        later.age == 7.0 && edge.reason == H::MarkReason::kMarkedKeptOpen && past.reason == H::MarkReason::kOverWindow,
                "corpse mark: an earlier mark keeps the window open until its end, inclusive (" + ReasonName(later.reason) + ", " +
                    ReasonName(past.reason) + " after)");
            H::ExtendWindow(t.slot, 106.0);
            a_check(t.slot.markedUntilGame == 108.0 && H::WindowOpen(t.slot, 107.0, kWindow),
                "corpse mark: a shorter extension does not shorten the window");
            (void)t.Record(Hit(H::Element::kFire, H::FireSource::kNpc, true, H::HitOrigin::kEffectStart, kFireboltMgef), 107.5);
            a_check(t.slot.markedUntilGame == 108.0 && t.Mark(107.6).reason == H::MarkReason::kNotStokedFire,
                "corpse mark: NPC fire inside a kept-open window still stops the mark (the window stays, the element decides)");
        }
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        RunRecord(a_check);
        RunMark(a_check);
    }
}
