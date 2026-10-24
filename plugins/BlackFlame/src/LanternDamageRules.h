#pragma once

#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>

// =============================================================================
// LANTERN DAMAGE RULES - the black flame (design v3, section 5): which of the player's hits set the target burning, the
// burn's per-second rate, and the per-target re-apply throttle, with no lock and no engine type (C++17:
// tools/blackflame-cosave-check tests them, LanternDamageRulesTests.h)
// =============================================================================
// LanternDamage.cpp fills SourceFacts once per hit source FormID and asks Classify; it asks IcdTable::TryAccept under its
// own mutex for every hit of an accepted source, and RateFor in the game-thread task. RE::FormID is std::uint32_t.

namespace BlackFlame::LanternRules
{
    /// The longest re-apply throttle the config accepts (Config.cpp lanternReapply); the table keeps an entry longer than this
    inline constexpr double kMaxReapplySeconds = 2.0;

    /// What the sink knows of a hit's source FormID
    struct SourceFacts
    {
        bool ownSpell = false;     // HoMBF_LanternBurnSpell itself (its own hit event)
        bool spellItem = false;    // the FormID is a SpellItem
        bool cloak = false;        // a fire cloak's associated spell (FireSet IsCloakSpell)
        bool hazardSpell = false;  // the spell of a fire hazard (BGSHazard data.spell)
        bool fireSpell = false;    // FireSet IsFireSpell
    };

    enum class Reject : std::uint8_t
    {
        kNone,  // accepted
        kOwnSpell,
        kNotASpell,
        kCloak,
        kNotFire
    };

    [[nodiscard]] inline const char* RejectName(Reject a_why) noexcept
    {
        switch (a_why) {
        case Reject::kOwnSpell:
            return "own spell";
        case Reject::kNotASpell:
            return "not a spell";
        case Reject::kCloak:
            return "cloak";
        case Reject::kNotFire:
            return "not a fire spell";
        default:
            return "accepted";
        }
    }

    /// Own spell, then not a spell, then cloak (a cloak's damage spell is also a fire spell, and a hazard could name it),
    /// then fire: a fire spell by FireSet or a fire hazard's spell. The casting type does not matter (v3: every hit is the
    /// same black flame); scrolls, staffs, shouts and powers are out because IsFireSpell counts spell type Spell only
    [[nodiscard]] inline Reject Classify(const SourceFacts& a_facts) noexcept
    {
        if (a_facts.ownSpell) {
            return Reject::kOwnSpell;
        }
        if (!a_facts.spellItem) {
            return Reject::kNotASpell;
        }
        if (a_facts.cloak) {
            return Reject::kCloak;
        }
        if (!a_facts.hazardSpell && !a_facts.fireSpell) {
            return Reject::kNotFire;
        }
        return Reject::kNone;
    }

    /// Config values (Config.h lanternBurnPct, lanternReapply) and the burn spell's effect duration
    struct Settings
    {
        double burnPct = 0.02;
        double reapplySeconds = 0.1;
        double burnSeconds = 2.0;  // HoMBF_LanternBurnSpell's effect duration, read at kDataLoaded (not a config key)
    };

    /// The burn's per-second rate (the spell's magnitude override): max health x pct / burn seconds.
    /// One complete burn deals max health x pct, with no flat damage. A maximum at or below 0 (or NaN)
    /// counts as 0; burn seconds at or below 0 gives 0 (LanternDamage turns itself off then).
    /// A negative or NaN result gives 0; the rate is capped at the largest float, the magnitude's type.
    [[nodiscard]] inline double RateFor(double a_maxHealth, double a_pct, double a_burnSeconds) noexcept
    {
        if (!(a_burnSeconds > 0.0)) {
            return 0.0;
        }
        const double max = a_maxHealth > 0.0 ? a_maxHealth : 0.0;
        const double rate = (max * a_pct) / a_burnSeconds;
        constexpr double kMaxRate = static_cast<double>(std::numeric_limits<float>::max());
        if (!(rate >= 0.0)) {
            return 0.0;
        }
        return rate < kMaxRate ? rate : kMaxRate;
    }

    /// The per-target re-apply throttle: one accepted hit per target per throttle. It changes nothing in the damage (a
    /// re-cast only restarts the burn); it keeps 60 hit events a second from casting 60 times a second. Entries older than
    /// kPruneAfter (past the longest throttle, so pruning never lets a hit through early) are dropped on each call (O(n)
    /// per call over the targets hit in the last 3 s: a handful in play)
    class IcdTable
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto kPruneAfter =
            std::chrono::milliseconds(static_cast<long long>((kMaxReapplySeconds + 1.0) * 1000.0));

        /// True (and the time noted) when this target has no entry or its last accepted hit is at least a_seconds ago;
        /// exactly the throttle passes
        bool TryAccept(std::uint32_t a_target, Clock::time_point a_now, double a_seconds)
        {
            Prune(a_now);
            const auto throttle = std::chrono::round<Clock::duration>(std::chrono::duration<double>(a_seconds));
            const auto it = m_last.find(a_target);
            if (it != m_last.end() && a_now - it->second < throttle) {
                return false;
            }
            m_last[a_target] = a_now;
            return true;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_last.size(); }

        void Clear() noexcept { m_last.clear(); }

    private:
        void Prune(Clock::time_point a_now)
        {
            for (auto it = m_last.begin(); it != m_last.end();) {
                it = a_now - it->second > kPruneAfter ? m_last.erase(it) : std::next(it);
            }
        }

        std::unordered_map<std::uint32_t, Clock::time_point> m_last;
    };
}
