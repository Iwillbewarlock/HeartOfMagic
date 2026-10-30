// progression-check's XP maths cases (ProgressionMath.h, ConfigRenames.h, RequiredXPTable.h),
// run by progression-check.cpp. Split from it to keep each file under 600 lines.

#include "progression-check.h"

#include "ProgressionMath.h"
#include "RequiredXPTable.h"
#include "uimanager/ConfigRenames.h"

#include <cstdint>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace ProgressionCheck
{
    namespace
    {
        namespace PM = ProgressionMath;
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
}
