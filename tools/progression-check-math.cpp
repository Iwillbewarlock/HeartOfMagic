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

        // The plugin's per-source counts (SpellProgress without the game types): shares of the required XP
        struct Shares
        {
            float shareAny = 0.0f;
            float shareSchool = 0.0f;
            float shareDirect = 0.0f;
            float shareSelf = 0.0f;
            std::unordered_map<std::string, float> shareModded;
        };

        // A cast's grant against the any cap, as OnSpellCast and AddSourcedXP do it: the room under the cap
        // (CapRoom), then the share the XP really added (ShareOf)
        float GrantAny(Shares& s, float gainXP, float requiredXP, float capShare)
        {
            const float grant = (std::min)(gainXP, PM::CapRoom(capShare, s.shareAny, requiredXP));
            s.shareAny += PM::ShareOf(grant, requiredXP);
            return grant;
        }

        // The per-source counts as shares (redesign 2026-11-04)
        void ShareCases()
        {
            constexpr float kCapAny = 0.05f;
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float inf = std::numeric_limits<float>::infinity();

            // Required XP changes in any order never reopen or inflate a cap
            Shares s;
            float given = GrantAny(s, 10.0f, 100.0f, kCapAny);   // 5 given, the cap (5% of 100) reached
            bool closed = Near(given, 5.0f) && Near(s.shareAny, kCapAny);
            for (const float required : { 50.0f, 400.0f, 100.0f, 1500.0f }) {
                closed = closed && Near(GrantAny(s, 10.0f, required, kCapAny), 0.0f) && Near(s.shareAny, kCapAny);
            }
            Check(closed, "shares: a spent any cap stays spent through required XP 100 -> 50 -> 400 -> 100 -> 1500");
            Shares half;
            GrantAny(half, 2.5f, 100.0f, kCapAny);   // half the any cap at 100
            const float atFour = GrantAny(half, 100.0f, 400.0f, kCapAny);
            Check(Near(atFour, 10.0f) && Near(half.shareAny, kCapAny),
                  "shares: half a cap used at 100 leaves half of it at 400 (10 XP of 20), not 17.5, not 0");

            // Unusable required XP never divides
            Check(!PM::IsUsableRequiredXP(0.0f) && !PM::IsUsableRequiredXP(-5.0f) && !PM::IsUsableRequiredXP(nan) &&
                      !PM::IsUsableRequiredXP(inf) && PM::IsUsableRequiredXP(1.0f),
                  "shares: a required XP of 0, negative, NaN or infinity is not usable");
            Check(Near(PM::ShareOf(5.0f, 0.0f), 0.0f) && Near(PM::ShareOf(5.0f, nan), 0.0f) && Near(PM::ShareOf(5.0f, inf), 0.0f) &&
                      Near(PM::ShareOf(inf, 100.0f), 0.0f) && Near(PM::XPOfShare(0.5f, inf), 0.0f) &&
                      Near(PM::CapRoom(kCapAny, 0.0f, nan), 0.0f),
                  "shares: XP <-> share and the cap room are 0 against an unusable required XP, never NaN or inf");
            Check(Near(PM::CleanShare(nan), 0.0f) && Near(PM::CleanShare(-0.2f), 0.0f) && Near(PM::CleanShare(3.0f), 1.0f) &&
                      Near(PM::CleanShare(0.4f), 0.4f),
                  "shares: a share read from a save is finite and in 0..1");

            // Fix A on shares: SetSpellXP lowering the progress
            Shares lowered;   // 75% held, any and school at their caps, then set back to 10%
            lowered.shareAny = 0.05f;
            lowered.shareSchool = 0.15f;
            lowered.shareSelf = 0.55f;
            Check(PM::FitSharesToProgress(lowered, 0.10f) &&
                      Near(lowered.shareAny + lowered.shareSchool + lowered.shareSelf, 0.10f) &&
                      PM::CapRoom(kCapAny, lowered.shareAny, 100.0f) > 0.0f,
                  "shares: SetSpellXP 75% -> 10% scales the shares to 10%, the any cap open again");
            Shares zero;
            zero.shareDirect = 0.5f;
            zero.shareModded["isl_study"] = 0.25f;
            Check(PM::FitSharesToProgress(zero, 0.0f) && Near(zero.shareDirect, 0.0f) && Near(zero.shareModded["isl_study"], 0.0f),
                  "shares: SetSpellXP 0 clears every share, modded too");
            Shares inStep;
            inStep.shareSchool = 0.15f;
            inStep.shareModded["passive"] = 0.050005f;
            Check(!PM::FitSharesToProgress(inStep, 0.20f) && Near(inStep.shareSchool, 0.15f),
                  "shares: a float hair over the progress is not touched");

            // ISL study: its count is a share too - the study cap (threshold share) holds whatever the required XP
            Shares study;
            const float threshold = 0.25f;
            const float firstXP = PM::StudyGrant(40.0f, 100.0f, threshold, PM::XPOfShare(0.0f, 100.0f));   // 25 of 100
            study.shareModded["isl_study"] = PM::ShareOf(firstXP, 100.0f);
            const float later = PM::StudyGrant(40.0f, 400.0f, threshold, PM::XPOfShare(study.shareModded["isl_study"], 400.0f));
            Check(Near(firstXP, 25.0f) && Near(study.shareModded["isl_study"], 0.25f) && Near(later, 0.0f),
                  "shares: ISL study capped at 25% of 100, still spent when the required XP is 400");

            // A 1.7 save's absolute counts converted once (SLPX v1 four, SLPR modded): XP / the required XP then
            CS::Entry old;
            old.progressPercent = 0.3f;
            old.xpFromAny = 5.0f;
            old.xpFromSchool = 15.0f;
            old.modded = { { "book_reading", 10.0f } };
            Shares converted;
            PM::SharesFromAbsolute(converted, old, 100.0f, old.progressPercent);
            Check(Near(converted.shareAny, 0.05f) && Near(converted.shareSchool, 0.15f) &&
                      Near(converted.shareModded["book_reading"], 0.10f),
                  "shares: a 1.7 save's 5/15/10 XP at a required 100 -> 5%/15%/10%");
            CS::Entry stuck;   // 1.7: progress lowered to 10% by cheat mode before fix A, counts at their caps
            stuck.progressPercent = 0.1f;
            stuck.xpFromAny = 5.0f;
            stuck.xpFromSchool = 15.0f;
            Shares unstuck;
            PM::SharesFromAbsolute(unstuck, stuck, 100.0f, stuck.progressPercent);
            Check(Near(unstuck.shareAny + unstuck.shareSchool, 0.10f) && PM::CapRoom(kCapAny, unstuck.shareAny, 100.0f) > 0.0f,
                  "shares: a 1.7 save stuck before fix A is held to its 10% at the conversion, the any cap open");
            Shares badRequired;
            PM::SharesFromAbsolute(badRequired, old, nan, old.progressPercent);
            Check(Near(badRequired.shareAny, 0.0f) && badRequired.shareModded.empty(),
                  "shares: no conversion against an unusable required XP (all 0, never NaN)");

            // SLPS round trip, cut short, a count too large, a long name
            const std::vector<CS::ShareEntry> entries = { { 0x0001'2FCD, 0.05f, 0.15f, 0.2f, 0.3f, { { "book_reading", 0.1f } } },
                                                          { 0xFE00'0801, 0.0f, 0.0f, 0.0f, 0.0f, { { "passive", 0.02f } } } };
            Bytes slps;
            std::vector<CS::ShareEntry> back;
            const bool io = CS::WriteShareEntries(slps, entries) &&
                            CS::ReadShareEntries(slps, static_cast<std::uint32_t>(slps.data.size()), back) == CS::ReadResult::Ok;
            Check(io && back.size() == 2 && Near(back[0].shareSchool, 0.15f) && back[0].modded.size() == 1 &&
                      back[0].modded[0].first == "book_reading" && Near(back[0].modded[0].second, 0.1f) &&
                      back[1].formId == 0xFE00'0801 && Near(back[1].modded[0].second, 0.02f),
                  "SLPS round trip: four shares and the modded ones per spell");
            Bytes cut;
            cut.data = slps.data;
            cut.data.resize(cut.data.size() - 2);
            Check(CS::ReadShareEntries(cut, static_cast<std::uint32_t>(slps.data.size()), back) == CS::ReadResult::ShortRead &&
                      back.size() == 1 && back[0].formId == 0x0001'2FCD,
                  "SLPS cut short: the whole entry before it kept");
            Bytes huge;
            const std::uint32_t many = 0x1000'0000;
            huge.WriteRecordData(&many, 4);
            Check(CS::ReadShareEntries(huge, 4, back) == CS::ReadResult::CountTooLarge && back.empty(),
                  "SLPS: a count larger than the record refused");
            Bytes longName;
            const std::uint32_t one = 1;
            const std::uint32_t id = 7;
            const float zeroShare = 0.0f;
            const std::uint32_t tooLong = CS::kMaxNameLength + 1;
            longName.WriteRecordData(&one, 4);
            longName.WriteRecordData(&id, 4);
            for (int i = 0; i < 4; ++i) longName.WriteRecordData(&zeroShare, 4);
            longName.WriteRecordData(&one, 4);
            longName.WriteRecordData(&tooLong, 4);
            Check(CS::ReadShareEntries(longName, 1 << 20, back) == CS::ReadResult::NameTooLong,
                  "SLPS: a modded name over the limit refused");

            // Per spell at load: an SLPS cut after entry 1 of 3 - spell 1 from its shares, spells 2 and 3 converted
            // from their absolute counts (5/15/50 of 100), not 0
            const std::vector<CS::ShareEntry> three = { { 1, 0.02f, 0.0f, 0.0f, 0.0f, {} }, { 2, 0.05f, 0.15f, 0.5f, 0.0f, {} },
                                                        { 3, 0.05f, 0.15f, 0.5f, 0.0f, {} } };
            Bytes threeBytes;
            std::vector<CS::ShareEntry> firstOnly;
            const bool wrote = CS::WriteShareEntries(threeBytes, three);
            const auto fullLength = static_cast<std::uint32_t>(threeBytes.data.size());
            threeBytes.data.resize(4 + 24 + 10);   // count, entry 1 (24 bytes), part of entry 2
            const bool cutRead = CS::ReadShareEntries(threeBytes, fullLength, firstOnly) == CS::ReadResult::ShortRead &&
                                 firstOnly.size() == 1 && firstOnly[0].formId == 1;
            CS::Entry absolute2;
            absolute2.progressPercent = 0.9f;
            absolute2.xpFromAny = 5.0f;
            absolute2.xpFromSchool = 15.0f;
            absolute2.xpFromDirect = 50.0f;
            Shares spell1, spell2, spell3;
            const int from1 = PM::CountsAtLoad(spell1, &firstOnly[0], &absolute2, 100.0f, 0.9f);
            const int from2 = PM::CountsAtLoad(spell2, static_cast<const CS::ShareEntry*>(nullptr), &absolute2, 100.0f, 0.9f);
            const int from3 = PM::CountsAtLoad(spell3, static_cast<const CS::ShareEntry*>(nullptr), &absolute2, 100.0f, 0.9f);
            Check(wrote && cutRead && from1 == 1 && Near(spell1.shareAny, 0.02f) && from2 == 2 && from3 == 2 &&
                      Near(spell2.shareAny, 0.05f) && Near(spell2.shareSchool, 0.15f) && Near(spell2.shareDirect, 0.5f) &&
                      Near(spell3.shareDirect, 0.5f),
                  "shares at load: an SLPS cut after entry 1 of 3 -> spell 1 from SLPS, spells 2-3 converted 5/15/50");
            Shares nothing;
            Check(PM::CountsAtLoad(nothing, static_cast<const CS::ShareEntry*>(nullptr), static_cast<const CS::Entry*>(nullptr),
                                   100.0f, 0.5f) == 0 && Near(nothing.shareAny, 0.0f),
                  "shares at load: a spell with neither keeps no counts");
            const CS::ShareEntry odd{ 4, 0.3f, 0.3f, 0.3f, 0.0f, { { "book_reading", 0.3f } } };   // 120% on a 40% spell
            Shares oddLoaded;
            PM::CountsAtLoad(oddLoaded, &odd, static_cast<const CS::Entry*>(nullptr), 100.0f, 0.4f);
            Check(Near(oddLoaded.shareAny + oddLoaded.shareSchool + oddLoaded.shareDirect + oddLoaded.shareModded["book_reading"], 0.4f),
                  "shares at load: SLPS shares summing above the progress are held to it");

            // SLPX still in 1.7.0's layout: 20 bytes per spell, XP = share x required at the save
            const std::vector<CS::CapEntry> absolute{ { 0x0001'2FCD, PM::XPOfShare(0.05f, 400.0f), PM::XPOfShare(0.15f, 400.0f), 0.0f, 0.0f } };
            Bytes slpx;
            Check(CS::WriteCapEntries(slpx, absolute) && slpx.data.size() == 4 + 20 && CS::kCapEntrySize == 20 &&
                      CS::kCapTrackersVersion == 1,
                  "SLPX written in 1.7.0's v1 layout: count + 20 bytes per spell");
            std::uint32_t count = 0;
            std::uint32_t formId = 0;
            float fields[4] = {};
            std::memcpy(&count, slpx.data.data(), 4);
            std::memcpy(&formId, slpx.data.data() + 4, 4);
            std::memcpy(fields, slpx.data.data() + 8, 16);
            Check(count == 1 && formId == 0x0001'2FCD && Near(fields[0], 20.0f) && Near(fields[1], 60.0f) && Near(fields[2], 0.0f),
                  "SLPX bytes as 1.7.0 reads them: id, then 20 and 60 XP (5% and 15% of 400)");
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
        // A (on shares): ShareCases, with the redesign's cases
        ShareCases();
        const float nan = std::numeric_limits<float>::quiet_NaN();

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
