#pragma once

// =============================================================================
// LANTERN DAMAGE RULES TESTS - plugins/BlackFlame/src/LanternDamageRules.h (design v3, the black flame): the source
// classification (own spell, not a spell, cloak, not fire, a hazard's spell), the burn rate maths, the per-target
// re-apply throttle at its exact edges and the map's pruning at the longest throttle
// =============================================================================
// Times are steady_clock points built from whole nanoseconds, so 0.1 s and 2 s are exact. Included by main.cpp.

#include "../../plugins/BlackFlame/src/LanternDamageRules.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace LanternDamageRulesTests
{
    namespace L = BlackFlame::LanternRules;
    using Clock = L::IcdTable::Clock;
    using std::chrono::milliseconds;
    using std::chrono::nanoseconds;
    using std::chrono::seconds;

    constexpr std::uint32_t kTarget = 0x0001A2B3;
    constexpr std::uint32_t kOther = 0xFF000801;
    constexpr double kTolerance = 1e-9;

    inline Clock::time_point At(nanoseconds a_since)
    {
        return Clock::time_point(std::chrono::duration_cast<Clock::duration>(a_since));
    }

    inline L::SourceFacts FireSpell()
    {
        L::SourceFacts facts;
        facts.spellItem = true;
        facts.fireSpell = true;
        return facts;
    }

    inline bool Near(double a_value, double a_expected)
    {
        return std::fabs(a_value - a_expected) <= kTolerance * (std::fabs(a_expected) > 1.0 ? std::fabs(a_expected) : 1.0);
    }

    template <class Check>
    void RunClassify(Check&& a_check)
    {
        a_check(L::Classify(FireSpell()) == L::Reject::kNone, "lantern: a fire spell is accepted (any casting type: v3 has no buckets)");
        auto hazard = FireSpell();
        hazard.fireSpell = false;
        hazard.hazardSpell = true;
        a_check(L::Classify(hazard) == L::Reject::kNone, "lantern: a fire hazard's spell is accepted though not itself a fire spell");

        auto own = FireSpell();
        own.ownSpell = true;
        auto ownHazard = own;
        ownHazard.hazardSpell = true;
        a_check(L::Classify(own) == L::Reject::kOwnSpell && L::Classify(ownHazard) == L::Reject::kOwnSpell,
            "lantern: the burn spell's own hit is rejected (own spell), first of all");
        auto cloak = FireSpell();
        cloak.cloak = true;
        auto cloakHazard = cloak;
        cloakHazard.hazardSpell = true;
        a_check(L::Classify(cloak) == L::Reject::kCloak && L::Classify(cloakHazard) == L::Reject::kCloak,
            "lantern: a cloak's damage spell is rejected (cloak), even if a hazard names it");
        L::SourceFacts weapon;
        auto frost = FireSpell();
        frost.fireSpell = false;
        a_check(L::Classify(weapon) == L::Reject::kNotASpell && L::Classify(frost) == L::Reject::kNotFire,
            "lantern: a non-spell source (weapon) and a non-fire spell are rejected");
        a_check(std::string(L::RejectName(L::Reject::kCloak)) == "cloak" && std::string(L::RejectName(L::Reject::kNone)) == "accepted",
            "lantern: reject names for the rejected line");
    }

    template <class Check>
    void RunRate(Check&& a_check)
    {
        // Config.h initialises lanternBurnPct, lanternBurnFlat and lanternReapply from Settings{} (review 2 I2: one source),
        // so these are the defaults the DLL uses; burnSeconds is replaced at kDataLoaded by the ESP's effect duration
        const L::Settings s;
        a_check(s.burnPct == 0.02 && s.burnFlat == 20.0 && s.reapplySeconds == 0.1 && s.burnSeconds == 2.0,
            "lantern: defaults (Config.h takes them from Settings) 2% + 20 over 2 s, re-apply 0.1 s");
        a_check(s.reapplySeconds >= 0.02 && s.reapplySeconds <= L::kMaxReapplySeconds && s.burnPct >= 0.0 && s.burnPct <= 1.0 &&
                    s.burnFlat >= 0.0 && s.burnFlat <= 10000.0,
            "lantern: the defaults are inside the ranges Config.cpp accepts for the json keys");
        const double r100 = L::RateFor(100.0, s.burnPct, s.burnFlat, s.burnSeconds);
        const double r1000 = L::RateFor(1000.0, s.burnPct, s.burnFlat, s.burnSeconds);
        a_check(Near(r100, 11.0) && Near(r100 * s.burnSeconds, 22.0),
            "lantern rate: max 100 -> 11/s, 22 over the 2 s burn (2 + 20), got " + std::to_string(r100));
        a_check(Near(r1000, 20.0) && Near(r1000 * s.burnSeconds, 40.0),
            "lantern rate: max 1000 -> 20/s, 40 over the burn (20 + 20), got " + std::to_string(r1000));
        a_check(Near(L::RateFor(0.0, s.burnPct, s.burnFlat, s.burnSeconds), 10.0) &&
                    Near(L::RateFor(-50.0, s.burnPct, s.burnFlat, s.burnSeconds), 10.0) &&
                    Near(L::RateFor(std::nan(""), s.burnPct, s.burnFlat, s.burnSeconds), 10.0),
            "lantern rate: max 0, negative or NaN counts as 0: the flat part still burns (10/s)");
        const double huge = 1.0e9;
        a_check(Near(L::RateFor(huge, s.burnPct, s.burnFlat, s.burnSeconds), (huge * 0.02 + 20.0) / 2.0),
            "lantern rate: max 1e9 -> 1e7 + 10 per second");
        constexpr double kFloatMax = static_cast<double>(std::numeric_limits<float>::max());
        const double capped = L::RateFor(kFloatMax, 1.0, 10000.0, 0.5);
        a_check(capped == kFloatMax && std::isfinite(static_cast<float>(capped)),
            "lantern rate: float max at 100% over 0.5 s is capped at the largest float (the magnitude stays finite)");
        a_check(L::RateFor(100.0, 0.02, 20.0, 0.0) == 0.0 && L::RateFor(100.0, 0.02, 20.0, -2.0) == 0.0 &&
                    L::RateFor(100.0, 0.02, 20.0, std::nan("")) == 0.0,
            "lantern rate: burn seconds 0, negative or NaN gives 0 (the feature is off then)");
        a_check(L::RateFor(100.0, -1.0, 0.0, 2.0) == 0.0 && L::RateFor(100.0, 0.0, 0.0, 2.0) == 0.0,
            "lantern rate: a negative result gives 0, pct 0 and flat 0 give 0");
    }

    template <class Check>
    void RunThrottle(Check&& a_check)
    {
        const auto t0 = At(seconds(100));
        {
            L::IcdTable table;
            const bool first = table.TryAccept(kTarget, t0, 0.1);
            const bool before = table.TryAccept(kTarget, t0 + milliseconds(100) - nanoseconds(1), 0.1);
            const bool edge = table.TryAccept(kTarget, t0 + milliseconds(100), 0.1);
            const bool again = table.TryAccept(kTarget, t0 + milliseconds(150), 0.1);
            a_check(first && !before && edge && !again,
                "lantern throttle: 0.1 s, a hit 1 ns before is refused, exactly 0.1 s passes, the edge restarts the throttle");
        }
        {
            L::IcdTable table;
            (void)table.TryAccept(kTarget, t0, 0.1);
            const bool otherTarget = table.TryAccept(kOther, t0, 0.1);
            a_check(otherTarget && table.Size() == 2, "lantern throttle: keyed by target (each its own entry)");
        }
        {
            // Flames held 5 s, a hit event every 1/60 s: one cast per 0.1 s (6 frames)
            L::IcdTable table;
            int accepted = 0;
            const auto frame = nanoseconds(16666667);
            for (int i = 0; i < 300; ++i) {
                accepted += table.TryAccept(kTarget, t0 + frame * i, 0.1) ? 1 : 0;
            }
            a_check(accepted == 50, "lantern throttle: Flames at 60 hit events/s for 5 s: " + std::to_string(accepted) + " casts (50)");
        }
        {
            // the longest throttle the config accepts: pruning must not let a hit through before it ends
            L::IcdTable table;
            const bool first = table.TryAccept(kTarget, t0, L::kMaxReapplySeconds);
            const bool early = table.TryAccept(kTarget, t0 + milliseconds(1500), L::kMaxReapplySeconds);
            const bool justBefore = table.TryAccept(kTarget, t0 + seconds(2) - nanoseconds(1), L::kMaxReapplySeconds);
            const bool end = table.TryAccept(kTarget, t0 + seconds(2), L::kMaxReapplySeconds);
            a_check(first && !early && !justBefore && end,
                "lantern throttle: the 2 s maximum refuses at 1.5 s and 1 ns before 2 s, passes at 2 s");
        }
        {
            const auto prune = L::IcdTable::kPruneAfter;
            L::IcdTable table;
            (void)table.TryAccept(kTarget, t0, 0.1);
            (void)table.TryAccept(kOther, t0 + prune, 0.1);
            const bool kept = table.Size() == 2;  // the first is exactly kPruneAfter old at the second call: kept
            (void)table.TryAccept(kOther, t0 + prune + nanoseconds(1), 0.1);
            const bool pruned = table.Size() == 1;  // now older than kPruneAfter: dropped
            const bool fresh = table.TryAccept(kTarget, t0 + prune + nanoseconds(2), 0.1);
            a_check(kept && pruned && fresh && prune == seconds(3) && prune > std::chrono::duration<double>(L::kMaxReapplySeconds),
                "lantern throttle: an entry exactly kPruneAfter (3 s) old is kept, one older is pruned on the next call");
            table.Clear();
            a_check(table.Size() == 0 && table.TryAccept(kTarget, t0, 0.1), "lantern throttle: Clear forgets every entry");
        }
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        RunClassify(a_check);
        RunRate(a_check);
        RunThrottle(a_check);
    }
}
