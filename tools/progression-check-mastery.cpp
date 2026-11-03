// progression-check's mastery rank cases (MasteryMath.h, ProgressionCoSave.h's SLMX), run by
// progression-check.cpp: the XP a rank costs by tier and multiplier, the rank a raw XP has
// reached, the skill factor, one cast's gain, the stop at the last rank, the settings reader and
// the SLMX record in a byte buffer (round trip, cut short, count too large, bad values dropped).

#include "progression-check.h"

#include "MasteryConfig.h"
#include "ProgressionCoSave.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace ProgressionCheck
{
    namespace
    {
        namespace MX = MasteryMath;
        namespace CS = ProgressionCoSave;

        constexpr std::size_t kNovice = 0;
        constexpr std::size_t kAdept = 2;
        constexpr std::size_t kMaster = 4;

        struct Writer
        {
            std::vector<std::uint8_t> bytes;
            bool WriteRecordData(const void* data, std::uint32_t length)
            {
                const auto* p = static_cast<const std::uint8_t*>(data);
                bytes.insert(bytes.end(), p, p + length);
                return true;
            }
            template <class T>
            void Put(const T& value) { WriteRecordData(&value, sizeof(T)); }
        };

        struct Reader
        {
            const std::vector<std::uint8_t>& bytes;
            std::size_t at = 0;
            std::uint32_t ReadRecordData(void* out, std::uint32_t length)
            {
                const auto n = static_cast<std::uint32_t>((std::min)(static_cast<std::size_t>(length), bytes.size() - at));
                if (n > 0) std::memcpy(out, bytes.data() + at, n);
                at += n;
                return n;
            }
        };

        void RankCases()
        {
            const MX::Settings s;   // defaults: 3 ranks, Novice 100 ... Master 1500, multipliers 1
            Check(Near(MX::RankCost(s, kNovice, 1), 100.0f) && Near(MX::RankCost(s, kMaster, 3), 1500.0f),
                  "mastery: a rank costs its tier's XP (Novice 100, Master 1500)");
            Check(Near(MX::TotalXP(s, kNovice), 300.0f) && Near(MX::TotalXP(s, kAdept), 1200.0f),
                  "mastery: the total is every rank's cost (Novice 300, Adept 1200)");
            Check(MX::RankFor(99.99f, s, kNovice) == 0 && MX::RankFor(100.0f, s, kNovice) == 1 &&
                      MX::RankFor(199.0f, s, kNovice) == 1 && MX::RankFor(200.0f, s, kNovice) == 2 &&
                      MX::RankFor(300.0f, s, kNovice) == 3,
                  "mastery: a rank is reached exactly at its floor (100, 200, 300), not a hair before");
            Check(MX::RankFor(10'000.0f, s, kNovice) == 3, "mastery: XP past the total is the last rank, no rank 4");
            const float nan = std::numeric_limits<float>::quiet_NaN();
            Check(MX::RankFor(0.0f, s, kNovice) == 0 && MX::RankFor(-5.0f, s, kNovice) == 0 &&
                      MX::RankFor(nan, s, kNovice) == 0,
                  "mastery: nothing, a negative value or NaN is rank 0");

            MX::Settings growing;
            growing.rankMultipliers = { 1.0f, 2.0f };   // shorter than 3 ranks: rank 3 repeats 2
            Check(Near(MX::RankCost(growing, kNovice, 2), 200.0f) && Near(MX::RankCost(growing, kNovice, 3), 200.0f) &&
                      Near(MX::TotalXP(growing, kNovice), 500.0f),
                  "mastery: a multiplier list shorter than the ranks repeats its last value (100 + 200 + 200)");
            MX::Settings empty;
            empty.rankMultipliers.clear();
            Check(Near(MX::RankCost(empty, kNovice, 2), 100.0f), "mastery: an empty multiplier list is 1");

            const auto p = MX::ProgressFor(150.0f, s, kNovice);
            Check(p.rank == 1 && p.ranks == 3 && Near(p.floor, 100.0f) && Near(p.next, 200.0f),
                  "mastery: 150 Novice XP is rank 1 of 3, between 100 and 200");
            const auto last = MX::ProgressFor(300.0f, s, kNovice);
            Check(last.rank == 3 && Near(last.floor, 300.0f) && Near(last.next, 300.0f),
                  "mastery: at the last rank the floor and the next are the total (a full bar)");
        }

        void GainCases()
        {
            const MX::Settings s;   // atMin 1, at100 2, xpRate 1
            Check(Near(MX::SkillFactor(0.0f, s), 1.0f) && Near(MX::SkillFactor(50.0f, s), 1.5f) &&
                      Near(MX::SkillFactor(75.0f, s), 1.75f) && Near(MX::SkillFactor(100.0f, s), 2.0f),
                  "mastery: skill factor one line for every tier: 1 at 0, 1.5 at 50, 1.75 at 75, 2 at 100");
            Check(Near(MX::SkillFactor(99.0f, s), 1.99f), "mastery: no step below 100 (99 gives 1.99, not 1)");
            Check(Near(MX::SkillFactor(150.0f, s), 2.0f), "mastery: a skill past 100 counts as 100");
            const float nan = std::numeric_limits<float>::quiet_NaN();
            Check(Near(MX::SkillFactor(nan, s), 1.0f) && Near(MX::SkillFactor(-20.0f, s), 1.0f),
                  "mastery: a NaN or negative skill is the factor at 0");
            MX::Settings flat;
            flat.skillAtMin = 2.0f;
            flat.skillAt100 = 0.5f;
            Check(Near(MX::SkillFactor(50.0f, flat), 1.25f), "mastery: a falling line (atMin above at100) is followed too");

            MX::Settings fast;
            fast.xpRate = 2.0f;
            Check(Near(MX::GainFor(5.0f, 50.0f, s), 7.5f) && Near(MX::GainFor(5.0f, 50.0f, fast), 15.0f),
                  "mastery: a cast's gain is base x xpRate x skill factor (5 x 1 x 1.5, 5 x 2 x 1.5)");
            Check(Near(MX::GainFor(0.0f, 50.0f, s), 0.0f) && Near(MX::GainFor(nan, 50.0f, s), 0.0f),
                  "mastery: no base XP or NaN gives nothing");

            // The stop at the last rank, and a lowered bar never cutting saved XP
            Check(Near(MX::Accumulate(290.0f, 25.0f, 300.0f), 300.0f), "mastery: a gain stops at the total");
            Check(Near(MX::Accumulate(300.0f, 25.0f, 300.0f), 300.0f), "mastery: at the total nothing more is kept");
            Check(Near(MX::Accumulate(500.0f, 25.0f, 300.0f), 500.0f),
                  "mastery: XP over a lowered total is kept as it is (not cut), nothing added");
            Check(Near(MX::Accumulate(10.0f, -3.0f, 300.0f), 10.0f) && Near(MX::Accumulate(10.0f, nan, 300.0f), 10.0f) &&
                      Near(MX::Accumulate(nan, 5.0f, 300.0f), 5.0f),
                  "mastery: a negative or NaN gain adds nothing, a NaN held value starts from 0");

            Check(MX::IsValidSavedXP(250.0f) && !MX::IsValidSavedXP(0.0f) && !MX::IsValidSavedXP(-1.0f) &&
                      !MX::IsValidSavedXP(nan) && !MX::IsValidSavedXP(std::numeric_limits<float>::infinity()) &&
                      !MX::IsValidSavedXP(MX::kMaxSavedXP * 2.0f),
                  "mastery: a saved value is kept only when positive, finite and reachable");
            // I3: which cast earns
            MX::CastFacts earns{ true, true, true, false, false, true };
            Check(MX::EarnsFromCast(earns), "mastery cast: a mastered tree spell the player has earns");
            MX::CastFacts target = earns;
            target.learningTarget = true;
            Check(!MX::EarnsFromCast(target), "mastery cast: a learning target earns none, even counted as mastered (copy known)");
            MX::CastFacts learning = earns;
            learning.stillLearning = true;
            MX::CastFacts off = earns;
            off.enabled = false;
            MX::CastFacts notMastered = earns;
            notMastered.mastered = false;
            Check(!MX::EarnsFromCast(learning) && !MX::EarnsFromCast(off) && !MX::EarnsFromCast(notMastered),
                  "mastery cast: weakened or ISL-held, ranks off, or not mastered: none");

            // I4: the read API answers 0 for a spell not mastered now
            Check(Near(MX::ReportedXP(true, 150.0f), 150.0f) && MX::RankFor(MX::ReportedXP(true, 150.0f), MX::Settings{}, 0) == 1,
                  "mastery read: a mastered spell reports its XP and rank");
            Check(Near(MX::ReportedXP(false, 150.0f), 0.0f) && MX::RankFor(MX::ReportedXP(false, 150.0f), MX::Settings{}, 0) == 0,
                  "mastery read: a spell not mastered now reports 0 XP and rank 0, its stored XP kept");
            Check(Near(MX::ReportedXP(true, nan), 0.0f) && Near(MX::ReportedXP(true, -4.0f), 0.0f),
                  "mastery read: a stored NaN or negative value reports 0");

            // B1: who keeps the ranks after a cheat-mode XP edit
            Check(MX::CountsAsMastered(true, 0.4f, false), "mastery kept: an unlocked spell set to 40% keeps its ranks");
            Check(MX::CountsAsMastered(false, 1.0f, false) && MX::CountsAsMastered(false, 0.0f, true),
                  "mastery kept: at 100%, or mastered another way (known from outside, a copy) at 0%");
            Check(!MX::CountsAsMastered(false, 0.99f, false) && !MX::CountsAsMastered(false, nan, false),
                  "mastery cleared: not unlocked, below 100% (or NaN), nothing else mastered");
            Check(MX::TierIndex("Master") == kMaster && MX::TierIndex("ADEPT") == kAdept && MX::TierIndex("") == kNovice &&
                      MX::TierIndex("Legendary") == kNovice,
                  "mastery: tier names in any case; an unknown tier is Novice");
        }

        void RekeyCases()
        {
            constexpr std::uint32_t kTree = 0x0001'2FCD;   // the tree's record
            constexpr std::uint32_t kCopy = 0xFE00'0801;   // a patch's copy of it
            constexpr std::uint32_t kOther = 0x0002'B96B;
            const auto keyOf = [](std::uint32_t id) { return id == kCopy ? kTree : id; };
            std::unordered_map<std::uint32_t, float> xp{ { kCopy, 120.0f }, { kTree, 40.0f }, { kOther, 7.0f } };
            const auto moved = MX::RekeyKeepLarger(xp, keyOf);
            Check(moved == 1 && xp.size() == 2 && Near(xp[kTree], 120.0f) && Near(xp[kOther], 7.0f),
                  "mastery rekey: a copy's entry moves to the tree's record, the larger XP kept on the collision");
            std::unordered_map<std::uint32_t, float> lone{ { kCopy, 30.0f } };
            Check(MX::RekeyKeepLarger(lone, keyOf) == 1 && lone.size() == 1 && Near(lone[kTree], 30.0f),
                  "mastery rekey: a copy alone moves whole");
            std::unordered_map<std::uint32_t, float> settled{ { kTree, 55.0f }, { kOther, 7.0f } };
            std::unordered_map<std::uint32_t, float> none;
            Check(MX::RekeyKeepLarger(settled, keyOf) == 0 && settled.size() == 2 && Near(settled[kTree], 55.0f) &&
                      MX::RekeyKeepLarger(none, keyOf) == 0 && none.empty(),
                  "mastery rekey: entries already on their key, and an empty map, change nothing");
            std::unordered_map<std::uint32_t, float> smaller{ { kCopy, 10.0f }, { kTree, 90.0f } };
            MX::RekeyKeepLarger(smaller, keyOf);
            Check(smaller.size() == 1 && Near(smaller[kTree], 90.0f), "mastery rekey: the tree record's larger XP wins over a copy's");
        }

        void SettingsCases()
        {
            using json = nlohmann::json;
            std::vector<std::string> problems;
            const auto defaults = MX::ReadSettings(json::object(), &problems);
            Check(defaults.enabled && defaults.ranks == 3 && Near(defaults.tierXP[4], 1500.0f) &&
                      defaults.rankMultipliers.size() == 3 && Near(defaults.skillAt100, 2.0f) && problems.empty(),
                  "mastery settings: no section -> on, 3 ranks, defaults, nothing to complain about");

            problems.clear();
            const auto read = MX::ReadSettings(json::parse(R"({"mastery": {"enabled": false, "ranks": 5,
                "tierXP": {"novice": 50, "master": 3000}, "rankMultipliers": [1, 1.5, 2], "skillFactor": {"atMin": 0.5,
                "at100": 3}, "xpRate": 2}})"),
                                               &problems);
            Check(!read.enabled && read.ranks == 5 && Near(read.tierXP[0], 50.0f) && Near(read.tierXP[1], 200.0f) &&
                      Near(read.tierXP[4], 3000.0f) && read.rankMultipliers.size() == 3 &&
                      Near(read.rankMultipliers[1], 1.5f) && Near(read.skillAtMin, 0.5f) && Near(read.skillAt100, 3.0f) &&
                      Near(read.xpRate, 2.0f) && problems.empty(),
                  "mastery settings: every key read, a tier left out keeps its default");

            problems.clear();
            const auto bad = MX::ReadSettings(json::parse(R"({"mastery": {"enabled": "yes", "ranks": 11,
                "tierXP": {"novice": -5, "adept": 0}, "rankMultipliers": [1, 0], "skillFactor": {"at100": "x"},
                "xpRate": 1e30}})"),
                                              &problems);
            Check(bad.enabled && bad.ranks == 3 && Near(bad.tierXP[0], 100.0f) && Near(bad.tierXP[2], 400.0f) &&
                      bad.rankMultipliers.size() == 3 && Near(bad.rankMultipliers[1], 1.0f) &&
                      Near(bad.skillAt100, 2.0f) && Near(bad.xpRate, 1.0f) && problems.size() == 7,
                  "mastery settings: wrong type, out of range, 0, negative, too large -> each the default, each named",
                  std::to_string(problems.size()) + " problems");

            problems.clear();
            const auto fractional = MX::ReadSettings(json::parse(R"({"mastery": {"ranks": 2.5}})"), &problems);
            const auto whole = MX::ReadSettings(json::parse(R"({"mastery": {"ranks": 4.0}})"));
            Check(fractional.ranks == 3 && problems.size() == 1 && whole.ranks == 4,
                  "mastery settings: ranks 2.5 refused (default 3), 4.0 read as 4");
            problems.clear();
            const auto notObject = MX::ReadSettings(json::parse(R"({"mastery": [1, 2]})"), &problems);
            Check(notObject.enabled && notObject.ranks == 3 && problems.size() == 1,
                  "mastery settings: a section that is not an object is the defaults, said once");
        }

        void CoSaveCases()
        {
            // Round trip
            const std::vector<CS::MasteryEntry> entries = { { 0x0001'2FCD, 150.0f }, { 0xFE00'0801, 2999.5f } };
            Writer w;
            Check(CS::WriteMasteryEntries(w, entries), "SLMX written");
            Check(w.bytes.size() == sizeof(std::uint32_t) + entries.size() * CS::kMasteryEntrySize,
                  "SLMX: count + 8 bytes per spell");
            std::vector<CS::MasteryEntry> back;
            Reader in{ w.bytes };
            Check(CS::ReadMasteryEntries(in, static_cast<std::uint32_t>(w.bytes.size()), back) == CS::ReadResult::Ok &&
                      back.size() == 2 && back[0].formId == 0x0001'2FCD && Near(back[0].xp, 150.0f) &&
                      back[1].formId == 0xFE00'0801 && Near(back[1].xp, 2999.5f),
                  "SLMX round trip: form ids and XP as written");

            // Cut short: the whole entries before it kept
            auto cut = w.bytes;
            cut.resize(cut.size() - 2);   // the last XP 2 of 4 bytes
            Reader cutIn{ cut };
            Check(CS::ReadMasteryEntries(cutIn, static_cast<std::uint32_t>(w.bytes.size()), back) ==
                          CS::ReadResult::ShortRead &&
                      back.size() == 1 && back[0].formId == 0x0001'2FCD,
                  "SLMX cut short: the whole entry before it kept, the cut one dropped");

            // A count the record cannot hold
            Writer huge;
            huge.Put(std::uint32_t{ 0x1000'0000 });
            Reader hugeIn{ huge.bytes };
            Check(CS::ReadMasteryEntries(hugeIn, 4, back) == CS::ReadResult::CountTooLarge && back.empty(),
                  "SLMX: a count larger than the record refused before anything is read");

            // Length mismatch: a record longer than its count says reads the count's entries only
            Writer longer = w;
            longer.Put(std::uint32_t{ 0xDEAD'BEEF });
            Reader longerIn{ longer.bytes };
            Check(CS::ReadMasteryEntries(longerIn, static_cast<std::uint32_t>(longer.bytes.size()), back) ==
                          CS::ReadResult::Ok &&
                      back.size() == 2,
                  "SLMX: bytes past the count's entries are not read as another spell");

            // Values a load must drop (the plugin keeps only IsValidSavedXP ones)
            Writer odd;
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const std::vector<CS::MasteryEntry> oddEntries = {
                { 1, nan }, { 2, -4.0f }, { 3, 0.0f }, { 4, std::numeric_limits<float>::infinity() }, { 5, 42.0f }
            };
            Check(CS::WriteMasteryEntries(odd, oddEntries), "SLMX with bad values written");
            Reader oddIn{ odd.bytes };
            const bool oddRead =
                CS::ReadMasteryEntries(oddIn, static_cast<std::uint32_t>(odd.bytes.size()), back) == CS::ReadResult::Ok;
            std::size_t kept = 0;
            for (const auto& e : back) kept += MX::IsValidSavedXP(e.xp) ? 1 : 0;
            Check(oddRead && back.size() == 5 && kept == 1, "SLMX: NaN, negative, 0 and infinite XP dropped on load, 42 kept");

            // Empty record
            Writer none;
            Check(CS::WriteMasteryEntries(none, {}) && none.bytes.size() == sizeof(std::uint32_t),
                  "SLMX with no spells is the count alone");
            Reader noneIn{ none.bytes };
            Check(CS::ReadMasteryEntries(noneIn, 4, back) == CS::ReadResult::Ok && back.empty(), "SLMX: empty reads empty");
        }
    }

    void MasteryCases()
    {
        RankCases();
        GainCases();
        RekeyCases();
        SettingsCases();
        CoSaveCases();
    }
}
