#pragma once
#include "core/NpcSpellsRules.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
// The dev-only in-game self test's pure parts (game/GameSelfTest.cpp does the spawning): its config file, the witness
// spells of one attached list, the tick-counted waits and a sample's verdict. The config file never ships (the code is
// in the DLL, idle without it): the test runs only when the file is present.
namespace NpcSpells {
    inline constexpr int kSelfTestDefaultStartDelayMs = 2000;   // after the player's 3D is in, before the first spawn
    inline constexpr int kSelfTestDefaultMaxWaitMs = 8000;      // per spawned actor: its 3D and high process must come in this time
    inline constexpr int kSelfTestMaxMs = 60000;                // either value above this (or below 0) goes back to its default
    inline constexpr int kSelfTestPollMs = 100;                 // one main-thread tick per this much (a worker thread queues it)
    inline constexpr int kSelfTestPlayerWaitMs = 60000;         // the player's 3D and cell, in ticks counted from the test's start
    struct SelfTestConfig {
        std::vector<FormRef> samples;            // base NPC (NPC_) or leveled NPC (LVLN) records, "XXXXXX:Plugin.ext" like the rules file
        std::vector<std::string> sampleTexts;    // as written, parallel to samples (for the log)
        int startDelayMs = kSelfTestDefaultStartDelayMs;
        int maxWaitMs = kSelfTestDefaultMaxWaitMs;
    };
    // {"samples": ["039D37:Skyrim.esm", ...], "startDelayMs": 2000, "maxWaitMs": 8000}. false + error when the text is not a JSON
    // object or no sample is usable; true otherwise, error naming each dropped sample or reset value ("; " between).
    bool ParseSelfTestConfig(const std::string& jsonText, SelfTestConfig& out, std::string& error);
    // A list's spells that prove the list worked: each once, in list order, without those the NPC can have another way
    // (excluded: its static spells, its race's, its other leveled lists' entries, the actor's added spells)
    std::vector<std::uint32_t> WitnessSpells(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded);
    // How many of a list's distinct spells the excluded set removed from its witnesses
    std::size_t CountExcluded(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded);
    // How many witnesses another of our lists on the same NPC also holds (a hit on one of them may have come from that list)
    std::size_t CountShared(const std::vector<std::uint32_t>& witnesses, const std::unordered_set<std::uint32_t>& otherLists);
    // A list's spells found in the actor's leveled spell cache, each once, in list order
    std::vector<std::uint32_t> PickedFrom(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& cache);
    // Waits are counted in executed ticks, not wall time: a game paused or in the background (no task runs) does not use
    // up a wait. true once `ticks` ticks of kSelfTestPollMs cover waitMs (0 ticks cover a wait of 0).
    bool SelfTestWaitOver(std::size_t ticks, int waitMs);
    enum class SelfTestVerdict { kPass, kFail, kInconclusive };
    struct SelfTestListOutcome {
        std::size_t witnesses = 0;   // spells only this list (of ours) can have given the NPC
        bool belowEvery = false;     // the actor's skill is below every entry: the list legitimately gives nothing
    };
    // PASS: a witness found on any list. INCONCLUSIVE: none found and no list could give one (each below every entry or
    // without witnesses). FAIL: none found where a list could give one, or no list at all. reason: for the RESULT line.
    SelfTestVerdict JudgeSample(const std::vector<SelfTestListOutcome>& lists, std::size_t distinctEvidence, std::string& reason);
    const char* VerdictName(SelfTestVerdict v);
}
