#pragma once

// =============================================================================
// LANTERN DAMAGE RULES TESTS - plugins/BlackFlame/src/LanternDamageRules.h: the bucket per casting type and hazard, the
// source classification (own spell, cloak, not a spell, not fire), the per-target ICD at its exact edges, the pct maths
// and the map's pruning
// =============================================================================
// Times are steady_clock points built from whole nanoseconds, so 0.25 s and 0.5 s are exact. Included by main.cpp.

#include "../../plugins/BlackFlame/src/LanternDamageRules.h"

#include <chrono>
#include <cstdint>
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

    inline Clock::time_point At(nanoseconds a_since)
    {
        return Clock::time_point(std::chrono::duration_cast<Clock::duration>(a_since));
    }

    inline L::SourceFacts FireSpell(std::uint32_t a_castingType)
    {
        L::SourceFacts facts;
        facts.spellItem = true;
        facts.fireSpell = true;
        facts.castingType = a_castingType;
        return facts;
    }

    template <class Check>
    void RunBuckets(Check&& a_check)
    {
        using B = L::Bucket;
        a_check(L::BucketFor(1, false) == B::kSingle && L::BucketFor(2, false) == B::kConcentration,
            "lantern: fire and forget is single, concentration is concentration");
        a_check(L::BucketFor(0, false) == B::kNone && L::BucketFor(3, false) == B::kNone && L::BucketFor(99, false) == B::kNone,
            "lantern: constant effect, scroll and an unknown casting type get no bucket");
        a_check(L::BucketFor(1, true) == B::kConcentration && L::BucketFor(0, true) == B::kConcentration,
            "lantern: a hazard's spell is concentration whatever its casting type");

        const auto firebolt = L::Classify(FireSpell(1));
        const auto flames = L::Classify(FireSpell(2));
        a_check(firebolt.bucket == B::kSingle && firebolt.why == L::Reject::kNone && flames.bucket == B::kConcentration,
            "lantern: a fire and forget fire spell is single, a concentration one concentration");
        auto hazard = FireSpell(1);
        hazard.fireSpell = false;
        hazard.hazardSpell = true;
        a_check(L::Classify(hazard).bucket == B::kConcentration,
            "lantern: a fire hazard's fire and forget spell (not itself a fire spell) is concentration, not single");

        auto own = FireSpell(1);
        own.ownSpell = true;
        auto cloak = FireSpell(1);
        cloak.cloak = true;
        auto cloakHazard = cloak;
        cloakHazard.hazardSpell = true;
        a_check(L::Classify(own).why == L::Reject::kOwnSpell && L::Classify(own).bucket == B::kNone,
            "lantern: the burn spell's own hit is rejected (own spell)");
        a_check(L::Classify(cloak).why == L::Reject::kCloak && L::Classify(cloakHazard).why == L::Reject::kCloak,
            "lantern: a cloak's damage spell is rejected (cloak), even if a hazard names it");
        L::SourceFacts weapon;
        weapon.castingType = 1;
        auto frost = FireSpell(1);
        frost.fireSpell = false;
        a_check(L::Classify(weapon).why == L::Reject::kNotASpell && L::Classify(frost).why == L::Reject::kNotFire,
            "lantern: a non-spell source and a non-fire spell are rejected");
        a_check(L::Classify(FireSpell(99)).why == L::Reject::kCastingType && L::Classify(FireSpell(3)).bucket == B::kNone,
            "lantern: a fire spell with an unknown casting type or a scroll's is rejected (casting type)");
    }

    template <class Check>
    void RunAmounts(Check&& a_check)
    {
        const L::Settings settings;
        a_check(L::PctFor(L::Bucket::kSingle, settings) == 0.02 && L::PctFor(L::Bucket::kConcentration, settings) == 0.004 &&
                    L::PctFor(L::Bucket::kNone, settings) == 0.0,
            "lantern: pct per bucket from the settings (none 0)");
        a_check(L::IcdFor(L::Bucket::kSingle, settings) == 0.5 && L::IcdFor(L::Bucket::kConcentration, settings) == 0.25,
            "lantern: ICD per bucket from the settings");
        a_check(L::DamageFor(250.0, 0.02) == 5.0 && L::DamageFor(1000.0, 0.004) == 4.0,
            "lantern: damage is maximum health times pct (250 x 2% = 5, 1000 x 0.4% = 4)");
        a_check(L::DamageFor(0.0, 0.02) == 0.0 && L::DamageFor(-10.0, 0.02) == 0.0, "lantern: no damage for a maximum at or below 0");
    }

    template <class Check>
    void RunIcd(Check&& a_check)
    {
        using B = L::Bucket;
        const auto t0 = At(seconds(100));
        {
            L::IcdTable table;
            const bool first = table.TryAccept(kTarget, B::kSingle, t0, 0.5);
            const bool before = table.TryAccept(kTarget, B::kSingle, t0 + milliseconds(500) - nanoseconds(1), 0.5);
            const bool edge = table.TryAccept(kTarget, B::kSingle, t0 + milliseconds(500), 0.5);
            a_check(first && !before && edge, "lantern ICD: single, a hit 1 ns before 0.5 s is refused, exactly 0.5 s passes");
        }
        {
            L::IcdTable table;
            const bool first = table.TryAccept(kTarget, B::kConcentration, t0, 0.25);
            const bool before = table.TryAccept(kTarget, B::kConcentration, t0 + milliseconds(250) - nanoseconds(1), 0.25);
            const bool edge = table.TryAccept(kTarget, B::kConcentration, t0 + milliseconds(250), 0.25);
            a_check(first && !before && edge, "lantern ICD: concentration, 1 ns before 0.25 s refused, exactly 0.25 s passes");
        }
        {
            L::IcdTable table;
            (void)table.TryAccept(kTarget, B::kSingle, t0, 0.5);
            const bool otherBucket = table.TryAccept(kTarget, B::kConcentration, t0, 0.25);
            const bool otherTarget = table.TryAccept(kOther, B::kSingle, t0, 0.5);
            a_check(otherBucket && otherTarget && table.Size() == 3, "lantern ICD: keyed by target and bucket (each its own entry)");
        }
        {
            // Flames held 5 s, a hit event every 1/60 s: at most 20 accepted, 20 x 0.4% = 8% of max (run sheet)
            L::IcdTable table;
            int accepted = 0;
            const auto frame = nanoseconds(16666667);
            for (int i = 0; i < 300; ++i) {
                accepted += table.TryAccept(kTarget, B::kConcentration, t0 + frame * i, 0.25) ? 1 : 0;
            }
            a_check(accepted == 20 && L::DamageFor(100.0, 0.004 * accepted) <= 8.0 + 1e-9,
                "lantern ICD: Flames at 60 hit events/s for 5 s: " + std::to_string(accepted) + " accepted (20), at most 8% of max");
        }
        {
            // the longest ICD the config accepts: pruning must not let a hit through before it ends (Codex review 1)
            L::IcdTable table;
            const bool first = table.TryAccept(kTarget, B::kSingle, t0, L::kMaxIcdSeconds);
            const bool early = table.TryAccept(kTarget, B::kSingle, t0 + seconds(6), L::kMaxIcdSeconds);
            const bool end = table.TryAccept(kTarget, B::kSingle, t0 + seconds(10), L::kMaxIcdSeconds);
            a_check(first && !early && end, "lantern ICD: a 10 s ICD refuses a hit after 6 s and passes at 10 s");
        }
        {
            const auto prune = L::IcdTable::kPruneAfter;
            L::IcdTable table;
            (void)table.TryAccept(kTarget, B::kSingle, t0, 0.5);
            (void)table.TryAccept(kOther, B::kSingle, t0 + prune, 0.5);
            const bool kept = table.Size() == 2;  // the first is exactly kPruneAfter old at the second call: kept
            (void)table.TryAccept(kOther, B::kConcentration, t0 + prune + nanoseconds(1), 0.25);
            const bool pruned = table.Size() == 2;  // now older than kPruneAfter: dropped, the new entry added
            const bool fresh = table.TryAccept(kTarget, B::kSingle, t0 + prune + nanoseconds(2), 0.5);
            a_check(kept && pruned && fresh && prune > seconds(10),
                "lantern ICD: an entry exactly kPruneAfter (11 s) old is kept, one older is pruned on the next call (and the target then passes)");
            table.Clear();
            a_check(table.Size() == 0 && table.TryAccept(kTarget, B::kSingle, t0, 0.5), "lantern ICD: Clear forgets every entry");
        }
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        RunBuckets(a_check);
        RunAmounts(a_check);
        RunIcd(a_check);
    }
}
