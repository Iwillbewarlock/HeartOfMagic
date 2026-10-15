// The in-game self test's pure parts (core/NpcSpellsSelfTest.h): its config file, witness, excluded and picked sets, tick-counted waits, reach, verdicts
#include "NpcSpellsCheck.h"
#include "core/NpcSpellsSelfTest.h"
#include <cstdio>
#include <format>
#include <string>
#include <vector>
using NpcSpellsCheck::Check;
using namespace NpcSpells;
int RunSelfTestSelfTest()
{
    std::printf("self test config and witnesses\n");
    {   // normal: three samples in the rules file's FormID form, both waits set
        SelfTestConfig c; std::string e;
        const bool ok = ParseSelfTestConfig(R"({"samples": ["039D37:Skyrim.esm", "013BBB:Skyrim.esm", "0551AD:Skyrim.esm"], "startDelayMs": 1500, "maxWaitMs": 9000})", c, e);
        Check(ok && e.empty() && c.samples.size() == 3 && c.sampleTexts.size() == 3, "three samples parse without an error");
        Check(c.samples[0].pluginLower == "skyrim.esm" && c.samples[0].local == 0x039D37 && c.sampleTexts[0] == "039D37:Skyrim.esm", "a sample keeps plugin (lower case), local id and its text");
        Check(c.startDelayMs == 1500 && c.maxWaitMs == 9000, "startDelayMs and maxWaitMs are read");
    }
    {   // edge: waits left out take the defaults; 0 and the cap are allowed
        SelfTestConfig c; std::string e;
        Check(ParseSelfTestConfig(R"({"samples": ["013BBB:Skyrim.esm"]})", c, e) && c.startDelayMs == kSelfTestDefaultStartDelayMs && c.maxWaitMs == kSelfTestDefaultMaxWaitMs, "missing waits take the defaults");
        Check(ParseSelfTestConfig(std::format(R"({{"samples": ["013BBB:Skyrim.esm"], "startDelayMs": 0, "maxWaitMs": {}}})", kSelfTestMaxMs), c, e) && e.empty() && c.startDelayMs == 0 && c.maxWaitMs == kSelfTestMaxMs, "0 and the cap are kept");
    }
    {   // edge: a bad sample is dropped alone, an out-of-range or non-integer wait goes back to its default, each named
        SelfTestConfig c; std::string e;
        const bool ok = ParseSelfTestConfig(R"({"samples": ["Skyrim.esm|0x039D37", 42, "013BBB:Skyrim.esm"], "startDelayMs": -1, "maxWaitMs": "8000"})", c, e);
        Check(ok && c.samples.size() == 1 && c.samples[0].local == 0x013BBB, "a persistentId and a number are dropped, the good sample stays");
        Check(c.startDelayMs == kSelfTestDefaultStartDelayMs && c.maxWaitMs == kSelfTestDefaultMaxWaitMs, "a negative and a string wait go back to the defaults");
        Check(e.find("Skyrim.esm|0x039D37") != std::string::npos && e.find("42") != std::string::npos && e.find("startDelayMs") != std::string::npos && e.find("maxWaitMs") != std::string::npos, "each problem is named in the error");
    }
    {   // failure: not JSON, not an object, no samples array, no usable sample
        SelfTestConfig c; std::string e;
        Check(!ParseSelfTestConfig("{ not json", c, e) && !e.empty(), "broken JSON is refused");
        e.clear(); Check(!ParseSelfTestConfig(R"(["013BBB:Skyrim.esm"])", c, e) && !e.empty(), "an array at the top is refused");
        e.clear(); Check(!ParseSelfTestConfig(R"({"maxWaitMs": 100})", c, e) && e.find("samples") != std::string::npos, "no samples array is refused");
        e.clear(); Check(!ParseSelfTestConfig(R"({"samples": ["Farengar"]})", c, e) && c.samples.empty() && e.find("no usable sample") != std::string::npos, "only bad samples is refused");
    }
    {   // witnesses: list order, duplicates once, excluded and null ids out
        const std::vector<std::uint32_t> list{ 0x10, 0x20, 0x10, 0x30, 0, 0x40 };
        const auto w = WitnessSpells(list, { 0x20, 0x99 });
        Check(w == std::vector<std::uint32_t>{ 0x10, 0x30, 0x40 }, "witnesses: list minus excluded, each once, in order, no null id");
        Check(WitnessSpells(list, {}).size() == 4, "witnesses: nothing excluded keeps every distinct id");
        Check(WitnessSpells({ 0x10, 0x20 }, { 0x10, 0x20 }).empty(), "witnesses: a list the NPC already has every spell of gives none");
        Check(WitnessSpells({}, { 0x10 }).empty(), "witnesses: an empty list gives none");
        Check(CountShared(w, { 0x30, 0x40, 0x77 }) == 2 && CountShared(w, {}) == 0, "shared counts witnesses another list holds");
        Check(CountExcluded(list, { 0x20, 0x99 }) == 1 && CountExcluded(list, {}) == 0, "excluded counts the list's distinct spells the set removed (not ids outside the list)");
        Check(CountExcluded({ 0x20, 0x20, 0 }, { 0x20, 0 }) == 1, "excluded counts a repeated spell once and never the null id");
    }
    {   // a leveled sample (LVLN) is written like any other record; the parser cannot tell the record type
        SelfTestConfig c; std::string e;
        const bool ok = ParseSelfTestConfig(R"({"samples": ["039D37:Skyrim.esm", "039D5F:Skyrim.esm"]})", c, e);
        Check(ok && e.empty() && c.samples.size() == 2 && c.samples[1].local == 0x039D5F && c.sampleTexts[1] == "039D5F:Skyrim.esm", "a leveled sample id parses like an NPC one");
    }
    {   // picked: the list's spells in the leveled spell cache, once, in list order
        Check(PickedFrom({ 0x10, 0x20, 0x10, 0x30 }, { 0x30, 0x10, 0x99 }) == std::vector<std::uint32_t>{ 0x10, 0x30 }, "picked: cache hits in list order, each once");
        Check(PickedFrom({ 0x10, 0x20 }, {}).empty() && PickedFrom({}, { 0x10 }).empty(), "picked: an empty cache or list picks nothing");
    }
    {   // tick-counted waits: ticks x kSelfTestPollMs against the wait
        Check(SelfTestWaitOver(0, 0) && !SelfTestWaitOver(0, 1), "a wait of 0 is over at once, any other is not");
        const auto ticks = static_cast<std::size_t>(kSelfTestDefaultMaxWaitMs / kSelfTestPollMs);
        Check(!SelfTestWaitOver(ticks - 1, kSelfTestDefaultMaxWaitMs) && SelfTestWaitOver(ticks, kSelfTestDefaultMaxWaitMs), "the default per-actor wait ends on its tick, not one before");
        Check(SelfTestWaitOver(static_cast<std::size_t>(kSelfTestPlayerWaitMs / kSelfTestPollMs), kSelfTestPlayerWaitMs) && SelfTestWaitOver(std::size_t{ 1 } << 40, kSelfTestMaxMs), "the player wait ends on its tick; a huge tick count does not overflow");
    }
    {   // reach: which entries a roll at the skill can pick (all-levels flag: every entry at or below; flag 0: the highest group)
        const std::vector<SelfTestEntry> e{ { 0x10, 0 }, { 0x20, 25 }, { 0x30, 25 }, { 0x40, 50 } };
        auto r = WitnessReach(e, true, 30.0f, { 0x10, 0x40 });
        Check(r.witnessReachable && r.minLevel == 0 && r.witnessMinLevel == 0 && r.topLevel == 25, "all levels: a witness below the skill is reachable even under a higher group");
        r = WitnessReach(e, false, 30.0f, { 0x10, 0x40 });
        Check(!r.witnessReachable && r.topLevel == 25, "flag 0: a witness only in a lower group than the highest at the skill is not reachable");
        Check(WitnessReach(e, false, 30.0f, { 0x30 }).witnessReachable && WitnessReach(e, false, 50.0f, { 0x40 }).witnessReachable, "flag 0: a witness in the highest group at the skill is reachable (a level equal to the skill counts)");
        r = WitnessReach(e, true, 20.0f, { 0x20, 0x40 });
        Check(!r.witnessReachable && r.witnessMinLevel == 25 && r.minLevel == 0, "all levels: the skill below every witness (an excluded entry below it does not count)");
        r = WitnessReach(e, false, -1.0f, { 0x10 });
        Check(!r.witnessReachable && r.topLevel == kSelfTestNoLevel, "a skill below every entry reaches nothing, no top level");
        Check(!WitnessReach({}, true, 100.0f, { 0x10 }).witnessReachable && !WitnessReach(e, true, 100.0f, {}).witnessReachable, "an empty list or no witnesses reaches nothing");
    }
    {   // verdicts
        std::string why;
        const SelfTestSampleLists both{ 2, 2 };
        Check(JudgeSample({ { 5, true }, { 3, false } }, both, 1, why) == SelfTestVerdict::kPass && why.find("1") != std::string::npos, "PASS: one witness found");
        Check(JudgeSample({ { 5, false } }, both, 2, why) == SelfTestVerdict::kPass, "PASS: a found witness counts even on a list with no reachable witness");
        Check(JudgeSample({ { 5, false }, { 0, true } }, both, 0, why) == SelfTestVerdict::kInconclusive && why.find("1 with no witness reachable") != std::string::npos && why.find("1 without") != std::string::npos, "INCONCLUSIVE: every list unreachable or without witnesses, each counted");
        Check(JudgeSample({ { 0, true } }, both, 0, why) == SelfTestVerdict::kInconclusive, "INCONCLUSIVE: lists with no witness at all");
        Check(JudgeSample({ { 5, true, 1, 0 }, { 4, false } }, both, 0, why) == SelfTestVerdict::kInconclusive && why.find("1 whose roll landed on non-witness") != std::string::npos, "INCONCLUSIVE: the cache shows the roll took a non-witness spell");
        Check(JudgeSample({ { 5, true, 1, 0 }, { 4, true } }, both, 0, why) == SelfTestVerdict::kFail && why.find("1 lists") != std::string::npos, "FAIL: another list could give a witness and gave none");
        Check(JudgeSample({ { 5, false }, { 4, true } }, both, 0, why) == SelfTestVerdict::kFail && why.find("1 lists") != std::string::npos, "FAIL: a list that could give a witness gave none");
        Check(JudgeSample({ { 5, false } }, { 9, 0 }, 0, why) == SelfTestVerdict::kFail && why.find("holder has 9 HoMNS lists, the spawned actor's base has none") != std::string::npos, "FAIL: lists on the holder, none on the spawned actor's base (even where nothing was reachable)");
        Check(JudgeSample({ { 5, true } }, { 9, 0 }, 3, why) == SelfTestVerdict::kPass, "PASS: a witness found wins over the missing base lists");
        Check(JudgeSample({}, { 0, 0 }, 0, why) == SelfTestVerdict::kFail && why.find("no HoMNS list") != std::string::npos, "FAIL: no list to check");
        Check(std::string(VerdictName(SelfTestVerdict::kInconclusive)) == "INCONCLUSIVE" && std::string(VerdictName(SelfTestVerdict::kPass)) == "PASS" && std::string(VerdictName(SelfTestVerdict::kFail)) == "FAIL", "verdict names");
    }
    return 0;
}
