#pragma once

#include <chrono>
#include <cstdint>
#include <iterator>
#include <unordered_map>

// =============================================================================
// LANTERN DAMAGE RULES - which of the player's hits get the lantern's extra damage, how much, and how often per target,
// with no lock and no engine type (C++17: tools/blackflame-cosave-check tests them, LanternDamageRulesTests.h)
// =============================================================================
// LanternDamage.cpp fills SourceFacts once per hit source FormID and asks Classify; it asks IcdTable::TryAccept under its
// own mutex for every hit of a classified source, and DamageFor in the game-thread task. RE::FormID is std::uint32_t.

namespace BlackFlame::LanternRules
{
    /// RE::MagicSystem::CastingType's values (LanternDamage.cpp static_asserts that they match)
    enum class CastingType : std::uint32_t
    {
        kConstantEffect = 0,
        kFireAndForget = 1,
        kConcentration = 2,
        kScroll = 3
    };

    /// single: fire and forget (2% per hit, 0.5 s per target by default); concentration: concentration spells and fire
    /// hazards' spells (0.4%, 0.25 s); none: no extra damage
    enum class Bucket : std::uint8_t
    {
        kNone,
        kSingle,
        kConcentration
    };

    [[nodiscard]] inline const char* BucketName(Bucket a_bucket) noexcept
    {
        switch (a_bucket) {
        case Bucket::kSingle:
            return "single";
        case Bucket::kConcentration:
            return "conc";
        default:
            return "none";
        }
    }

    /// A hazard's spell is concentration whatever its own casting type (a hazard hits every tick: a Wall of Flames hazard
    /// in the 2% bucket could reach 4%/s, design review R5); otherwise fire and forget is single, concentration is
    /// concentration, anything else (constant effect, scroll, a value no engine type has) none
    [[nodiscard]] inline Bucket BucketFor(std::uint32_t a_castingType, bool a_hazardSpell) noexcept
    {
        if (a_hazardSpell) {
            return Bucket::kConcentration;
        }
        switch (static_cast<CastingType>(a_castingType)) {
        case CastingType::kFireAndForget:
            return Bucket::kSingle;
        case CastingType::kConcentration:
            return Bucket::kConcentration;
        default:
            return Bucket::kNone;
        }
    }

    /// What the sink knows of a hit's source FormID
    struct SourceFacts
    {
        bool ownSpell = false;     // HoMBF_LanternBurnSpell itself (its own hit event)
        bool spellItem = false;    // the FormID is a SpellItem
        bool cloak = false;        // a fire cloak's associated spell (FireSet IsCloakSpell)
        bool hazardSpell = false;  // the spell of a fire hazard (BGSHazard data.spell)
        bool fireSpell = false;    // FireSet IsFireSpell
        std::uint32_t castingType = 0;
    };

    enum class Reject : std::uint8_t
    {
        kNone,
        kOwnSpell,
        kNotASpell,
        kCloak,
        kNotFire,
        kCastingType
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
        case Reject::kCastingType:
            return "casting type";
        default:
            return "accepted";
        }
    }

    struct Classified
    {
        Bucket bucket = Bucket::kNone;
        Reject why = Reject::kNone;  // kNone exactly when bucket is not kNone
    };

    /// Own spell, then not a spell, then cloak (a cloak's damage spell is fire and forget and also a fire spell), then a
    /// fire hazard's spell (before the casting type: hazard spells are fire and forget), then fire by FireSet, then the bucket
    [[nodiscard]] inline Classified Classify(const SourceFacts& a_facts) noexcept
    {
        if (a_facts.ownSpell) {
            return { Bucket::kNone, Reject::kOwnSpell };
        }
        if (!a_facts.spellItem) {
            return { Bucket::kNone, Reject::kNotASpell };
        }
        if (a_facts.cloak) {
            return { Bucket::kNone, Reject::kCloak };
        }
        if (!a_facts.hazardSpell && !a_facts.fireSpell) {
            return { Bucket::kNone, Reject::kNotFire };
        }
        const Bucket bucket = BucketFor(a_facts.castingType, a_facts.hazardSpell);
        return { bucket, bucket == Bucket::kNone ? Reject::kCastingType : Reject::kNone };
    }

    /// Config values (Config.h lanternSinglePct ... lanternConcIcd)
    struct Settings
    {
        double singlePct = 0.02;
        double singleIcd = 0.5;  // seconds
        double concPct = 0.004;
        double concIcd = 0.25;  // seconds
    };

    [[nodiscard]] inline double PctFor(Bucket a_bucket, const Settings& a_settings) noexcept
    {
        return a_bucket == Bucket::kSingle ? a_settings.singlePct : a_bucket == Bucket::kConcentration ? a_settings.concPct : 0.0;
    }

    [[nodiscard]] inline double IcdFor(Bucket a_bucket, const Settings& a_settings) noexcept
    {
        return a_bucket == Bucket::kSingle ? a_settings.singleIcd : a_bucket == Bucket::kConcentration ? a_settings.concIcd : 0.0;
    }

    /// The hidden spell's magnitude: maximum health times the bucket's share (0 for a maximum at or below 0)
    [[nodiscard]] inline double DamageFor(double a_maxHealth, double a_pct) noexcept
    {
        return a_maxHealth > 0.0 ? a_maxHealth * a_pct : 0.0;
    }

    /// The per-target cooldown: one accepted hit per (target, bucket) per ICD. Entries older than kPruneAfter are
    /// dropped on each call (O(n) per call over the targets hit in the last 5 s: a handful in play)
    class IcdTable
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto kPruneAfter = std::chrono::seconds(5);

        /// True (and the time noted) when this target's bucket has no entry or its last accepted hit is at least
        /// a_icdSeconds ago; exactly the ICD passes (the run sheet asks damage lines >= 0.25 s apart)
        bool TryAccept(std::uint32_t a_target, Bucket a_bucket, Clock::time_point a_now, double a_icdSeconds)
        {
            Prune(a_now);
            const auto icd = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(a_icdSeconds));
            const auto key = Key(a_target, a_bucket);
            const auto it = m_last.find(key);
            if (it != m_last.end() && a_now - it->second < icd) {
                return false;
            }
            m_last[key] = a_now;
            return true;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_last.size(); }

        void Clear() noexcept { m_last.clear(); }

    private:
        static std::uint64_t Key(std::uint32_t a_target, Bucket a_bucket) noexcept
        {
            return (static_cast<std::uint64_t>(a_target) << 8) | static_cast<std::uint64_t>(a_bucket);
        }

        void Prune(Clock::time_point a_now)
        {
            for (auto it = m_last.begin(); it != m_last.end();) {
                it = a_now - it->second > kPruneAfter ? m_last.erase(it) : std::next(it);
            }
        }

        std::unordered_map<std::uint64_t, Clock::time_point> m_last;
    };
}
