// progression-check's XP maths cases (ProgressionMath.h, ConfigRenames.h, RequiredXPTable.h),
// run by progression-check.cpp. Split from it to keep each file under 600 lines.

#include "progression-check.h"

#include "ProgressionCoSave.h"
#include "ProgressionMath.h"
#include "RequiredXPTable.h"
#include "uimanager/ConfigRenames.h"

#include <algorithm>
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
        namespace PM = ProgressionMath;
        namespace CS = ProgressionCoSave;

        // SKSE's record stream shapes over a byte buffer (as progression-check.cpp's)
        struct Bytes
        {
            std::vector<std::uint8_t> data;
            std::size_t at = 0;
            bool WriteRecordData(const void* p, std::uint32_t n)
            {
                const auto* b = static_cast<const std::uint8_t*>(p);
                data.insert(data.end(), b, b + n);
                return true;
            }
            std::uint32_t ReadRecordData(void* out, std::uint32_t n)
            {
                const auto got = static_cast<std::uint32_t>((std::min)(static_cast<std::size_t>(n), data.size() - at));
                if (got > 0) std::memcpy(out, data.data() + at, got);
                at += got;
                return got;
            }
        };

        struct Counts
        {
            float xpFromAny = 0.0f;
            float xpFromSchool = 0.0f;
            float xpFromDirect = 0.0f;
            float xpFromSelf = 0.0f;
            std::unordered_map<std::string, float> xpFromModded;
        };

        // A load's entry as the plugin holds it (SpellProgress without the game types)
        struct Entry : Counts
        {
            float progressPercent = 0.0f;
            float requiredXP = 0.0f;
            bool fitPending = false;
        };

        // An entry from a 1.7 save (SLPX v1, no basis) stuck before fix A: 10% of 100 XP held, but the
        // any and school counts at their caps (5 and 15) from before a cheat-mode lowering
        Entry StuckV1()
        {
            Entry e;
            e.progressPercent = 0.1f;
            e.requiredXP = 100.0f;
            e.xpFromAny = 5.0f;
            e.xpFromSchool = 15.0f;
            return e;
        }

        // The one-time fit of a v1 save's counts: at the tree's numbers, or the first XP without a tree
        void PendingFitCases()
        {
            constexpr float kCapAny = 5.0f;   // percent
            const float anyCapAt100 = 100.0f * kCapAny / 100.0f;

            // v1 load -> marked; a required XP change before the fit does not scale the unknown basis;
            // the tree's numbers in (SettlePendingTrackerFits) -> fitted to the 10 XP held, the any cap open
            Entry tree = StuckV1();
            const bool marked = PM::MarkFitPending(tree);
            const bool held = !PM::RescaleUnlessPending(tree, 100.0f, 400.0f) && Near(tree.xpFromAny, 5.0f);
            Check(marked && tree.fitPending && held && Near(PM::BasisToSave(tree), 0.0f),
                  "pending fit: a v1 entry is marked, not scaled by a required XP change, saved with no basis");
            const bool settled = PM::SettlePendingFit(tree);
            Check(settled && !tree.fitPending && Near(tree.xpFromAny + tree.xpFromSchool, 10.0f) &&
                      PM::GrantWithinCap(1.0f, anyCapAt100, tree.xpFromAny) > 0.0f && Near(PM::BasisToSave(tree), 100.0f),
                  "pending fit: tree applied -> the stuck counts fitted to the 10 XP held, the any cap open again");
            Check(!PM::SettlePendingFit(tree) && Near(tree.xpFromAny, 2.5f), "pending fit: once only");

            // v1 load, no tree this session: the spell's first XP fits it (the cast loop, AddSourcedXP)
            Entry cast = StuckV1();
            PM::MarkFitPending(cast);
            const bool firstXP = PM::SettlePendingFit(cast);
            Check(firstXP && Near(cast.xpFromAny, 2.5f) && Near(cast.xpFromSchool, 7.5f) &&
                      Near(PM::GrantWithinCap(1.0f, anyCapAt100, cast.xpFromAny), 1.0f),
                  "pending fit: no tree, first cast -> fitted, the any cap gives again");

            // v2 load: has a basis, never marked; scaled as B2 says, never fitted
            Entry v2 = StuckV1();
            v2.fitPending = false;
            const bool scaled = PM::RescaleUnlessPending(v2, 100.0f, 200.0f);
            Check(scaled && !PM::SettlePendingFit(v2) && Near(v2.xpFromAny, 10.0f) && Near(v2.xpFromSchool, 30.0f),
                  "pending fit: a v2 entry is not marked or fitted, its counts only scaled with the required XP");

            // An entry with no counts is not marked
            Entry none;
            none.progressPercent = 0.3f;
            none.requiredXP = 100.0f;
            Check(!PM::MarkFitPending(none) && !none.fitPending, "pending fit: an entry with no counts is not marked");
        }

        // B2: the per-source counts against the required XP, through a save and a load whose first
        // required XP is passing (this character's number back from the panel later, the tree's late)
        void TrackerShareCases()
        {
            constexpr float kSaved = 500.0f;     // the required XP when the game was saved
            constexpr float kPassing = 100.0f;   // what the load works out first (tier only)
            constexpr float kCapAny = 5.0f;      // percent
            Counts c;
            c.xpFromAny = 25.0f;                 // the any cap (5% of 500) closed
            c.xpFromSchool = 30.0f;
            c.xpFromSelf = 100.0f;
            c.xpFromModded["book_reading"] = 20.0f;

            // Save (SLPX v2 with the required XP), load
            Bytes record;
            const std::vector<CS::CapEntry> saved{ { 0x0001'2FCD, c.xpFromAny, c.xpFromSchool, c.xpFromDirect, c.xpFromSelf, kSaved } };
            std::vector<CS::CapEntry> back;
            const bool io = CS::WriteCapEntries(record, saved) &&
                            CS::ReadCapEntries(record, CS::kCapTrackersVersion, static_cast<std::uint32_t>(record.data.size()),
                                               back) == CS::ReadResult::Ok;
            Check(io && back.size() == 1 && Near(back[0].requiredXP, kSaved) && Near(back[0].xpFromAny, 25.0f),
                  "tracker share: SLPX v2 carries the required XP the counts were made against");

            // EndLoad: the passing required XP - the counts keep their share, the any cap stays closed
            Counts loaded = c;
            Check(PM::RescaleTrackers(loaded, back[0].requiredXP, kPassing) && Near(loaded.xpFromAny, 5.0f) &&
                      Near(loaded.xpFromModded["book_reading"], 4.0f) &&
                      Near(PM::GrantWithinCap(3.0f, kPassing * kCapAny / 100.0f, loaded.xpFromAny), 0.0f),
                  "tracker share: load at a passing 100 -> any 5 of a 5 cap, still closed");
            // The real number arrives (SetRequiredXP): back to the saved counts, still closed
            Check(PM::RescaleTrackers(loaded, kPassing, kSaved) && Near(loaded.xpFromAny, 25.0f) &&
                      Near(loaded.xpFromSchool, 30.0f) && Near(loaded.xpFromSelf, 100.0f) &&
                      Near(loaded.xpFromModded["book_reading"], 20.0f) &&
                      Near(PM::GrantWithinCap(3.0f, kSaved * kCapAny / 100.0f, loaded.xpFromAny), 0.0f),
                  "tracker share: then 500 again -> the saved counts back, the any cap never reopened");

            // A required XP lowered then raised (override toggled, the share slider)
            Counts toggled = c;
            PM::RescaleTrackers(toggled, 500.0f, 250.0f);
            PM::RescaleTrackers(toggled, 250.0f, 500.0f);
            Check(Near(toggled.xpFromAny, 25.0f) && Near(toggled.xpFromSchool, 30.0f) &&
                      Near(toggled.xpFromModded["book_reading"], 20.0f),
                  "tracker share: 500 -> 250 -> 500 gives every count back");

            // A v1 save (no required XP), equal numbers, bad numbers: nothing scaled
            Bytes v1;
            v1.data.resize(sizeof(std::uint32_t) + CS::kCapEntrySizeV1);
            const std::uint32_t one = 1;
            const float v1Fields[] = { 7.0f, 0.0f, 0.0f, 0.0f };
            const std::uint32_t id = 0x0001'2FCD;
            std::memcpy(v1.data.data(), &one, 4);
            std::memcpy(v1.data.data() + 4, &id, 4);
            std::memcpy(v1.data.data() + 8, v1Fields, sizeof(v1Fields));
            std::vector<CS::CapEntry> v1Back;
            const bool v1Ok = CS::ReadCapEntries(v1, 1, static_cast<std::uint32_t>(v1.data.size()), v1Back) == CS::ReadResult::Ok;
            Counts v1Counts;
            v1Counts.xpFromAny = 7.0f;
            Check(v1Ok && v1Back.size() == 1 && Near(v1Back[0].xpFromAny, 7.0f) && Near(v1Back[0].requiredXP, 0.0f) &&
                      !PM::RescaleTrackers(v1Counts, v1Back[0].requiredXP, kPassing) && Near(v1Counts.xpFromAny, 7.0f),
                  "tracker share: a v1 save reads with no required XP, its counts kept as saved");
            const float nan = std::numeric_limits<float>::quiet_NaN();
            Counts same = c;
            Check(!PM::RescaleTrackers(same, 500.0f, 500.0f) && !PM::RescaleTrackers(same, 0.0f, 500.0f) &&
                      !PM::RescaleTrackers(same, 500.0f, nan) && !PM::RescaleTrackers(same, -3.0f, 500.0f) &&
                      Near(same.xpFromAny, 25.0f),
                  "tracker share: an equal, 0, negative or NaN required XP scales nothing");
        }
    }

    void MathCases()
    {
        // Caps
        Check(Near(PM::GrantWithinCap(10.0f, 50.0f, 45.0f), 5.0f), "cap: grant cut to what is left");
        Check(Near(PM::GrantWithinCap(10.0f, 50.0f, 50.0f), 0.0f), "cap: nothing at the cap");
        Check(Near(PM::GrantWithinCap(10.0f, 50.0f, 60.0f), 0.0f) && Near(PM::GrantWithinCap(-3.0f, 50.0f, 0.0f), 0.0f),
              "cap: over the cap or a negative amount gives 0");

        // Passive: the tier cap holds after the overall multiplier
        const float ask = PM::PreMultiplierGrant(10.0f, 6.0f, 2.0f);
        Check(Near(ask, 3.0f) && Near(ask * 2.0f, 6.0f), "passive: asks 3 so x2 lands on the 6 left");
        Check(Near(PM::PreMultiplierGrant(10.0f, 60.0f, 1.0f), 10.0f), "passive: under the cap asks the full amount");
        Check(Near(PM::PreMultiplierGrant(10.0f, 6.0f, 0.0f), 0.0f) && Near(PM::PreMultiplierGrant(10.0f, 0.0f, 1.0f), 0.0f),
              "passive: no multiplier or no room gives 0");
        // Passive's Master cap of 50% is no longer cut at the modded default of 25%
        const float required = 1500.0f;
        const float masterCapXP = required * 0.50f;
        float tracked = 0.0f;
        for (int hour = 0; hour < 1000; ++hour) {
            tracked += PM::PreMultiplierGrant(5.0f, PM::CapRemaining(masterCapXP, tracked), 1.0f);
        }
        Check(Near(tracked, masterCapXP), "passive: a 50% tier cap fills to 50%, not 25%",
              std::to_string(tracked) + " of " + std::to_string(masterCapXP));

        // SetSpellXP
        Check(Near(PM::ProgressForXP(50.0f, 100.0f), 0.5f), "SetSpellXP: 50 of 100 is 0.5");
        Check(Near(PM::ProgressForXP(200.0f, 100.0f), 1.0f) && Near(PM::ProgressForXP(100.0f, 100.0f), 1.0f),
              "SetSpellXP: 200 of 100 clamps to 1.0");
        Check(Near(PM::ProgressForXP(-5.0f, 100.0f), 0.0f) && Near(PM::ProgressForXP(50.0f, 0.0f), 0.0f),
              "SetSpellXP: negative XP or no required XP gives 0");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(Near(PM::ProgressForXP(nan, 100.0f), 0.0f) && Near(PM::ProgressForXP(50.0f, nan), 0.0f) &&
                  Near(PM::ProgressForXP(std::numeric_limits<float>::infinity(), 100.0f), 1.0f),
              "SetSpellXP: NaN gives 0 (not a NaN progress), infinity clamps to 1");
        Check(Near(PM::GrantWithinCap(nan, 50.0f, 0.0f), 0.0f) && Near(PM::PreMultiplierGrant(nan, 6.0f, 1.0f), 0.0f) &&
                  Near(PM::StudyGrant(nan, 400.0f, 0.25f, 0.0f), 0.0f),
              "caps: a NaN amount grants 0");

        // ISL study: swapping books restarts ISL's count, not the cap
        const float req = 400.0f;
        const float threshold = 0.25f;
        float studied = 0.0f;
        for (int book = 0; book < 4; ++book) {
            for (int hour = 0; hour < 8; ++hour) {
                studied += PM::StudyGrant((1.0f / 8.0f) * req * threshold, req, threshold, studied);
            }
        }
        Check(Near(studied, req * threshold), "study: four books read in full give one threshold share (100 of 400)",
              std::to_string(studied));
        Check(Near(PM::StudyGrant(30.0f, req, threshold, 90.0f), 10.0f), "study: a session is cut at the share");
        Check(Near(PM::StudyGrant(30.0f, req, 0.0f, 0.0f), 0.0f) && Near(PM::StudyGrant(-1.0f, req, threshold, 0.0f), 0.0f),
              "study: no threshold or a negative session gives 0");

        // AddXPNoGrant stops below 1.0
        Check(Near(PM::NoGrantProgress(0.1f, 10.0f, 100.0f), 0.2f), "no-grant: 0.1 + 10 of 100 is 0.2");
        Check(Near(PM::NoGrantProgress(0.9f, 50.0f, 100.0f), PM::kNoGrantMaxProgress),
              "no-grant: stops at kNoGrantMaxProgress, not 1.0");
        Check(Near(PM::NoGrantProgress(1.0f, 10.0f, 100.0f), 1.0f) && Near(PM::NoGrantProgress(0.5f, 10.0f, 0.0f), 0.0f),
              "no-grant: a save already at 1.0 is not lowered; no required XP gives 0");

        // Mastery
        // ShouldMaster(old, new, unlocked, earlyLearned, playerHasSpell)
        Check(PM::ShouldMaster(0.95f, 1.0f, false, true, true), "mastery: weakened spell crossing 1.0 masters");
        Check(PM::ShouldMaster(0.95f, 1.0f, false, false, false),
              "mastery: crossing 1.0 for a spell not early learned (a copy known) masters as before");
        Check(!PM::ShouldMaster(0.95f, 1.0f, false, true, false),
              "mastery: crossing 1.0 while held for ISL and not taught stays ready");
        Check(PM::ShouldMaster(1.0f, 1.0f, false, true, true),
              "mastery: stuck at 1.0, early learned and taught (ISL overflow) masters");
        Check(!PM::ShouldMaster(1.0f, 1.0f, false, true, false),
              "mastery: stuck at 1.0, held for ISL and not taught, waits");
        Check(!PM::ShouldMaster(1.0f, 1.0f, false, false, false),
              "mastery: stuck at 1.0, never granted (cheat 100%), stays ready for Unlock");
        Check(PM::ShouldMaster(0.95f, 1.0f, true, false, true),
              "mastery: an unlocked spell lowered by cheat mode masters again when it crosses 1.0");
        Check(!PM::ShouldMaster(1.0f, 1.0f, true, true, true) && !PM::ShouldMaster(0.5f, 0.99f, false, true, true),
              "mastery: already unlocked at 1.0, or below 1.0, does not master");

        // Early grant: ShouldTryEarlyGrant(old, new, threshold, isTarget, alreadyGranted)
        Check(PM::ShouldTryEarlyGrant(0.20f, 0.30f, 0.25f, true, false) &&
                  PM::ShouldTryEarlyGrant(0.20f, 0.30f, 0.25f, false, false),
              "early grant: crossing the threshold grants, target or not");
        Check(PM::ShouldTryEarlyGrant(0.22f, 0.22f, 0.20f, true, false) &&
                  PM::ShouldTryEarlyGrant(0.22f, 0.23f, 0.20f, true, false),
              "early grant: a target at 22% after the threshold went 25% -> 20% grants (re-check and next XP)");
        Check(!PM::ShouldTryEarlyGrant(0.22f, 0.23f, 0.20f, false, false),
              "early grant: a spell no longer the target, above the threshold, is not granted again");
        Check(!PM::ShouldTryEarlyGrant(0.10f, 0.15f, 0.20f, true, false), "early grant: below the threshold, nothing");
        Check(!PM::ShouldTryEarlyGrant(0.20f, 0.30f, 0.25f, true, true) &&
                  !PM::ShouldTryEarlyGrant(0.50f, 0.60f, 0.25f, true, true),
              "early grant: already granted (early learned or held for ISL), nothing");
        Check(!PM::ShouldTryEarlyGrant(1.0f, 1.0f, 0.25f, true, false) && PM::ShouldTryEarlyGrant(0.9f, 1.0f, 0.25f, true, false),
              "early grant: a target at 1.0 before (cheat 100%) stays ready; reaching 1.0 now grants");
        Check(!PM::ShouldTryEarlyGrant(0.5f, 0.5f, nan, true, false), "early grant: a NaN threshold grants nothing");

        // Passive learning ticks in game hours (Calendar::GetHoursPassed)
        const auto hour = PM::PassiveTickFor(100.0f, 101.0f);
        Check(Near(hour.grantHours, 1.0f) && Near(hour.lastHours, 101.0f),
              "passive: one game hour later grants one hour");
        Check(Near(PM::PassiveXPFor(5.0f, 24.0f), 5.0f) && Near(PM::PassiveXPFor(5.0f, 1.0f), 5.0f / 24.0f),
              "passive: 5 XP a game day gives 5 over 24 game hours, 5/24 for one hour");
        Check(Near(PM::PassiveXPFor(5.0f, 0.0f), 0.0f) && Near(PM::PassiveXPFor(0.0f, 24.0f), 0.0f) &&
                  Near(PM::PassiveXPFor(-1.0f, 24.0f), 0.0f),
              "passive: no time, or no / a negative rate, gives nothing");
        // The old setting name moves over before the defaults (new name) are merged
        {
            auto old = nlohmann::json::parse(R"({"passiveLearning":{"enabled":true,"xpPerGameHour":20}})");
            ConfigRenames::Migrate(old);
            Check(old["passiveLearning"].value("xpPerGameDay", 0) == 20 && !old["passiveLearning"].contains("xpPerGameHour"),
                  "config: a saved xpPerGameHour of 20 becomes xpPerGameDay 20, the old name dropped");
            auto both = nlohmann::json::parse(R"({"passiveLearning":{"xpPerGameHour":20,"xpPerGameDay":7}})");
            ConfigRenames::Migrate(both);
            Check(both["passiveLearning"].value("xpPerGameDay", 0) == 7 && !both["passiveLearning"].contains("xpPerGameHour"),
                  "config: with both names the new one stands");
            auto odd = nlohmann::json::parse(R"({"passiveLearning":5,"other":{"xpPerGameHour":3}})");
            ConfigRenames::Migrate(odd);
            auto none = nlohmann::json::parse(R"({"hotkeyCode":66})");
            ConfigRenames::Migrate(none);
            Check(odd["passiveLearning"] == 5 && odd["other"].contains("xpPerGameHour") && none.size() == 1,
                  "config: no passiveLearning object, or none at all, is left as it is");
        }
        const auto small = PM::PassiveTickFor(100.0f, 100.05f);
        Check(Near(small.grantHours, 0.0f) && Near(small.lastHours, 100.0f),
              "passive: under 0.1 h grants nothing and keeps the last time (short ticks add up)");
        const auto atFloor = PM::PassiveTickFor(100.0f, 100.0f + PM::kPassiveMinHours);
        Check(Near(atFloor.grantHours, 0.0f), "passive: exactly 0.1 h is not past the floor");
        const auto back = PM::PassiveTickFor(100.0f, 50.0f);
        Check(Near(back.grantHours, 0.0f) && Near(back.lastHours, 50.0f),
              "passive: time going back grants nothing and starts from now");
        const auto nanTick = PM::PassiveTickFor(100.0f, nan);
        Check(Near(nanTick.grantHours, 0.0f), "passive: a NaN time grants nothing");
        // On or off: the poll loop's ticks, 3 game hours apart, from a load at hour 100
        {
            float last = 100.0f;   // OnGameLoad: the save's time
            float paid = 0.0f;
            auto run = [&](float now, bool on) {
                const auto t = PM::PassiveTickFor(last, now, on);
                last = t.lastHours;
                paid += t.grantHours;
            };
            run(103.0f, true);
            run(106.0f, true);
            Check(Near(paid, 6.0f) && Near(last, 106.0f), "passive: on all along pays every hour since the load");
            paid = 0.0f;
            run(130.0f, false);
            run(150.0f, false);
            Check(Near(paid, 0.0f) && Near(last, 150.0f), "passive: while off nothing is paid and the last time follows the clock");
            run(152.0f, true);
            Check(Near(paid, 2.0f), "passive: switched on, only the 2 hours since the last tick off are paid (not 46)");
            last = 200.0f;   // another save loaded at hour 200
            paid = 0.0f;
            run(200.05f, true);
            run(201.0f, true);
            Check(Near(paid, 1.0f), "passive: the first ticks after a load pay from the load's time");
            // A wait of 8 hours between the last tick off (hour 300) and the switch: SetSettings
            // resets the last time at the switch (an off tick at that moment), the wait unpaid
            last = 300.0f;
            paid = 0.0f;
            run(308.0f, false);   // SetSettings on the off -> on change
            run(309.0f, true);
            Check(Near(paid, 1.0f), "passive: a wait just before switching on is not paid (reset at the switch)");
        }

        // AddXP's progress; a required XP not set leaves the progress alone
        Check(Near(PM::ProgressAfterXP(0.2f, 10.0f, 100.0f), 0.3f), "AddXP: 0.2 + 10 of 100 is 0.3");
        Check(Near(PM::ProgressAfterXP(0.95f, 50.0f, 100.0f), 1.0f), "AddXP: stops at 1");
        Check(Near(PM::ProgressAfterXP(0.4f, 10.0f, 0.0f), 0.4f) && Near(PM::ProgressAfterXP(0.4f, 10.0f, -5.0f), 0.4f) &&
                  Near(PM::ProgressAfterXP(0.4f, 10.0f, nan), 0.4f),
              "AddXP: required XP 0, negative or NaN keeps the progress (was wiped to 0)");
    }

    // Required XP by kind (RequiredXPTable.h): what a revert keeps
    void RequiredXPCases()
    {
        using RequiredXP::Kind;
        RequiredXP::Table table;
        constexpr std::uint32_t kSpell = 0x0001'2FCD;
        constexpr float kTier = 100.0f;
        table.Set(Kind::kTree, kSpell, 150.0f);
        table.SetOverrides({ { kSpell, 400.0f } });
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 400.0f), "required XP: the settings' override wins over the tree");
        table.Set(Kind::kCharacter, kSpell, 120.0f);
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 120.0f), "required XP: this character's number wins");
        table.OnRevert();   // a reload
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 400.0f),
              "required XP: after a reload the override still applies (only the character's number went)");
        Check(Near(table.For(kSpell, kTier, true, 0.3f), 120.0f),
              "required XP: the reverse unlock share on the override, as the panel works it out");
        // The panel's Math.max(1, Math.round(base * share))
        Check(Near(RequiredXP::Table::Shared(2.0f, 0.3f), 1.0f), "required XP: 2 at 30% is 1 (not 0.6)");
        Check(Near(RequiredXP::Table::Shared(1.0f, 0.4f), 1.0f) && Near(RequiredXP::Table::Shared(1.0f, 0.1f), 1.0f),
              "required XP: a share under 1 XP is 1 at least (0.4, 0.1)");
        Check(Near(RequiredXP::Table::Shared(5.0f, 0.3f), 2.0f) && Near(RequiredXP::Table::Shared(5.0f, 0.5f), 3.0f) &&
                  Near(RequiredXP::Table::Shared(333.0f, 0.3f), 100.0f),
              "required XP: rounded to whole XP as Math.round (1.5 -> 2, 2.5 -> 3, 99.9 -> 100)");
        table.SetOverrides({ { 0x0400'0001, 2.0f } });
        Check(Near(table.For(0x0400'0001, kTier, true, 0.3f), 1.0f) && Near(table.For(0x0400'0002, 7.0f, true, 0.3f), 2.0f),
              "required XP: the rounded share on an override and on the tier");
        table.SetOverrides({ { kSpell, 400.0f } });
        table.SetOverrides({});
        Check(Near(table.For(kSpell, kTier, false, 1.0f), 150.0f), "required XP: override removed - the tree's number");
        Check(Near(table.For(0x0300'0D62, kTier, false, 1.0f), kTier), "required XP: nothing known - the tier's");
        table.Set(Kind::kCharacter, 0x0300'0D62, -5.0f);
        Check(Near(table.For(0x0300'0D62, kTier, false, 1.0f), kTier) && !table.HasCharacterValue(0x0300'0D62),
              "required XP: a non-positive number counts as none");
    }

    // The XP fixes of 2026-11-03 (the four losses before 100%)
    void XPFixCases()
    {
        TrackerShareCases();
        PendingFitCases();

        // A: the per-source counts never stay over the XP the spell holds
        struct FakeProgress
        {
            float xpFromAny = 0.0f;
            float xpFromSchool = 0.0f;
            float xpFromDirect = 0.0f;
            float xpFromSelf = 0.0f;
            std::unordered_map<std::string, float> xpFromModded;
        };
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(Near(PM::TrackerScale(40.0f, 30.0f), 1.0f) && Near(PM::TrackerScale(40.0f, 40.0f), 1.0f),
              "trackers: counts at or under the XP held are left alone");
        Check(Near(PM::TrackerScale(40.0f, 80.0f), 0.5f), "trackers: counts of 80 on 40 XP held scale by 0.5");
        Check(Near(PM::TrackerScale(0.0f, 50.0f), 0.0f) && Near(PM::TrackerScale(-5.0f, 50.0f), 0.0f) &&
                  Near(PM::TrackerScale(nan, 50.0f), 0.0f),
              "trackers: no XP held (0, negative, NaN) clears every count");
        FakeProgress lowered;   // cheat mode set 75 of 100 XP back to 10: any and school were at their caps
        lowered.xpFromAny = 5.0f;
        lowered.xpFromSchool = 15.0f;
        lowered.xpFromSelf = 55.0f;
        lowered.xpFromModded["book_reading"] = 0.0f;
        Check(PM::FitTrackersToXP(lowered, 10.0f) && Near(lowered.xpFromAny, 5.0f * 10.0f / 75.0f) &&
                  Near(lowered.xpFromSchool, 15.0f * 10.0f / 75.0f) && Near(lowered.xpFromSelf, 55.0f * 10.0f / 75.0f) &&
                  Near(lowered.xpFromAny + lowered.xpFromSchool + lowered.xpFromSelf, 10.0f),
              "trackers: 75 counted on 10 XP held -> each count scaled, the sum 10, the any cap open again");
        FakeProgress cleared;
        cleared.xpFromDirect = 50.0f;
        cleared.xpFromModded["isl_study"] = 25.0f;
        Check(PM::FitTrackersToXP(cleared, 0.0f) && Near(cleared.xpFromDirect, 0.0f) &&
                  Near(cleared.xpFromModded["isl_study"], 0.0f),
              "trackers: SetSpellXP 0 clears the built-in and modded counts");
        FakeProgress inStep;
        inStep.xpFromSchool = 15.0f;
        inStep.xpFromModded["passive"] = 5.0005f;   // 20.0005 counted on 20 held
        Check(!PM::FitTrackersToXP(inStep, 20.0f) && Near(inStep.xpFromSchool, 15.0f),
              "trackers: counts a float hair over the XP held are not touched");

        // B: a cast of another plugin's copy of a linked spell is a direct cast
        const std::vector<std::uint32_t> targetPrereqs{ 0x0001'2FCD, 0x0001'2FD0 };   // Flames, Frostbite
        const auto listed = [&](std::uint32_t id) {
            return std::find(targetPrereqs.begin(), targetPrereqs.end(), id) != targetPrereqs.end();
        };
        const std::vector<std::uint32_t> flamesCopies{ 0xFE00'0801 };   // a patch's Flames
        const std::vector<std::uint32_t> none;
        Check(PM::SpellOrCopyMatches(0x0001'2FCD, none, listed), "direct link: the listed spell itself");
        Check(PM::SpellOrCopyMatches(0xFE00'0801, std::vector<std::uint32_t>{ 0x0001'2FCD }, listed),
              "direct link: a copy of a listed spell counts (its group holds the listed one)");
        Check(!PM::SpellOrCopyMatches(0x0002'B96B, none, listed) && !PM::SpellOrCopyMatches(0x0002'B96B, flamesCopies, listed),
              "direct link: an unlisted spell with no copy listed is not one");

        // C: what a grant really added (the progress AddXP moved), not what was asked
        const float at99 = PM::ProgressAfterXP(0.99f, 5.0f, 100.0f);
        Check(Near(PM::ActualRise(0.99f, at99, 100.0f), 1.0f), "real XP: 99% + 5 of 100 adds 1, not 5");
        Check(Near(PM::ActualRise(0.2f, PM::ProgressAfterXP(0.2f, 10.0f, 100.0f), 100.0f), 10.0f),
              "real XP: under the top the whole grant counts (20% + 10)");
        Check(Near(PM::ActualRise(1.0f, PM::ProgressAfterXP(1.0f, 15.0f, 100.0f), 100.0f), 0.0f),
              "real XP: a spell at 100% not unlocked takes nothing (BookXP keeps the book)");
        Check(Near(PM::ActualRise(0.5f, 0.3f, 100.0f), 0.0f) && Near(PM::ActualRise(0.2f, 0.4f, 0.0f), 0.0f) &&
                  Near(PM::ActualRise(nan, 0.4f, 100.0f), 0.0f),
              "real XP: a lowered progress, no required XP or NaN is 0, never negative");

        // D: single mode credits the closest link first, whatever order the targets map holds
        using PM::CastSource;
        std::vector<std::string> keys{ "Illusion", "Alteration", "Destruction", "Conjuration", "Restoration" };
        PM::OrderBySource(keys, { CastSource::kAny, CastSource::kSchool, CastSource::kSelf, CastSource::kAny,
                                  CastSource::kDirect });
        Check(keys == std::vector<std::string>{ "Destruction", "Restoration", "Alteration", "Illusion", "Conjuration" },
              "single mode: self, then direct, school, any (two any targets keep their order)");
        std::vector<std::string> one{ "Destruction" };
        PM::OrderBySource(one, { CastSource::kAny });
        std::vector<std::string> empty;
        PM::OrderBySource(empty, {});
        Check(one.size() == 1 && empty.empty(), "single mode: one target or none is left as it is");
        std::vector<std::string> mismatch{ "a", "b" };
        PM::OrderBySource(mismatch, { CastSource::kAny });
        Check(mismatch == std::vector<std::string>{ "a", "b" }, "single mode: sources of another length change nothing");
        Check(std::string(PM::CastSourceName(CastSource::kDirect)) == "direct" &&
                  std::string(PM::CastSourceName(CastSource::kSelf)) == "self",
              "single mode: the source names the XP log uses");
    }
}
