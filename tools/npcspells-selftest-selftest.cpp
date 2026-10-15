// The in-game self test's pure parts (core/NpcSpellsSelfTest.h): its config file and the witness spells of a list
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
    }
    return 0;
}
