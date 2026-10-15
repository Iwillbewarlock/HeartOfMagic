#pragma once
#include "core/NpcSpellsRules.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
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
    inline constexpr int kSelfTestDefaultRepeat = 1;            // spawns of one sample, one after another
    inline constexpr int kSelfTestMaxRepeat = 10;               // a repeat outside 1..this goes back to the default
    struct SelfTestConfig {
        std::vector<FormRef> samples;            // base NPC (NPC_) or leveled NPC (LVLN) records, "XXXXXX:Plugin.ext" like the rules file
        std::vector<std::string> sampleTexts;    // as written, parallel to samples (for the log)
        std::vector<int> repeats;                // parallel to samples: how many times each is spawned (one after another)
        int startDelayMs = kSelfTestDefaultStartDelayMs;
        int maxWaitMs = kSelfTestDefaultMaxWaitMs;
    };
    // {"samples": ["039D37:Skyrim.esm", {"id": "013BBB:Skyrim.esm", "repeat": 5}, ...], "startDelayMs": 2000, "maxWaitMs": 8000}:
    // a sample is its FormID text, or an object with "id" and an optional "repeat" (1..kSelfTestMaxRepeat). false + error when
    // the text is not a JSON object or no sample is usable; true otherwise, error naming each dropped sample or reset value ("; " between).
    bool ParseSelfTestConfig(const std::string& jsonText, SelfTestConfig& out, std::string& error);
    // The sample as the per-run lines name it: the text alone for a sample spawned once, "<text> run=k/N" for a repeated one
    std::string SelfTestRunLabel(std::string_view sampleText, int run, int runs);
    // A repeated sample's runs: per list (EditorID, first-seen order), how many checked runs found one of its spells in the
    // actor's leveled spell cache (picked > 0). Lists of one school overlap, so a pick may have come from another list of ours.
    struct SelfTestRunTally {
        std::size_t checkedRuns = 0;
        std::vector<std::pair<std::string, std::size_t>> pickedRuns;
        void AddRun(const std::vector<std::pair<std::string, bool>>& listPicked);   // one checked run: (EditorID, picked > 0) per list
    };
    // "SELFTEST lists <text> runs=N checked=C <EditorID> pickedRuns=K/C, ..." (the repeated sample's final line)
    std::string SelfTestListsLine(std::string_view sampleText, int runs, const SelfTestRunTally& tally);
    // A list's spells that prove the list worked: each once, in list order, without those the NPC can have another way
    // (excluded: the static spells and other leveled lists' entries of the sample, its holder, its bases and their template
    // chains, its race's spells and leveled lists, the actor's added spells)
    std::vector<std::uint32_t> WitnessSpells(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded);
    // How many of a list's distinct spells the excluded set removed from its witnesses
    std::size_t CountExcluded(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& excluded);
    // How many witnesses another of our lists on the same NPC also holds (a hit on one of them may have come from that list)
    std::size_t CountShared(const std::vector<std::uint32_t>& witnesses, const std::unordered_set<std::uint32_t>& otherLists);
    // A list's spells found in the actor's leveled spell cache, each once, in list order
    std::vector<std::uint32_t> PickedFrom(const std::vector<std::uint32_t>& listSpells, const std::unordered_set<std::uint32_t>& cache);
    // Waits are counted in executed ticks, not wall time: a game window in the background (alt-tab: no task runs) does not use
    // up a wait. true once `ticks` ticks of kSelfTestPollMs cover waitMs (0 ticks cover a wait of 0).
    bool SelfTestWaitOver(std::size_t ticks, int waitMs);
    enum class SelfTestVerdict { kPass, kFail, kInconclusive };
    inline constexpr std::uint16_t kSelfTestNoLevel = 0xFFFF;   // "no such entry" in SelfTestReach
    struct SelfTestEntry { std::uint32_t spell = 0; std::uint16_t level = 0; };  // one leveled list entry, in list order
    struct SelfTestReach {
        bool witnessReachable = false;                  // a roll at this skill can land on a witness
        std::uint16_t minLevel = kSelfTestNoLevel;      // the lowest entry level
        std::uint16_t witnessMinLevel = kSelfTestNoLevel;  // the lowest level of a witness entry
        std::uint16_t topLevel = kSelfTestNoLevel;      // the highest entry level at or below the skill
    };
    // Which entries a roll at `skill` can pick: with the all-levels flag (llFlags bit 0) any entry at or below the skill,
    // without it only the entries of the highest level at or below the skill (README "Lists"). Reachable when one of
    // those is a witness.
    SelfTestReach WitnessReach(const std::vector<SelfTestEntry>& entries, bool allLevels, float skill, const std::unordered_set<std::uint32_t>& witnesses);
    struct SelfTestListOutcome {
        std::size_t witnesses = 0;        // spells only this list (of ours) can have given the NPC
        bool witnessReachable = true;     // false: at the actor's skill the list's roll cannot land on a witness
        std::size_t picked = 0;           // the list's spells in the actor's leveled spell cache
        std::size_t pickedWitnesses = 0;  // of them, witnesses
    };
    struct SelfTestSampleLists { std::size_t holder = 0, actorBase = 0; };  // our lists on the spell list holder and the spawned actor's base
    // PASS: a witness found on any list. FAIL "holder has N HoMNS lists, the spawned actor's base has none": nothing found
    // and the lists did not reach the record the engine rolls from. INCONCLUSIVE: nothing found and no list could give a
    // witness (no witnesses, none reachable at the skill, or the cache shows the roll landed on non-witnesses). FAIL:
    // nothing found where a list could give one, or no list at all. reason: for the RESULT line.
    SelfTestVerdict JudgeSample(const std::vector<SelfTestListOutcome>& lists, SelfTestSampleLists where, std::size_t distinctEvidence, std::string& reason);
    const char* VerdictName(SelfTestVerdict v);
    // A leveled sample (LVLN) is spawned by the console's placeatme run on the player, which returns no ref: the actor is
    // looked for among the actors that appeared near the player since. One candidate per such actor that has
    // ExtraLeveledCreature and is no other sample's.
    struct LeveledSpawnCandidate {
        std::uint32_t refId = 0;
        bool originalIsSample = false;   // ExtraLeveledCreature::originalBase is the sample's LVLN record
        bool templateIsLeaf = false;     // its template base is an NPC entry of the LVLN (nested LVLNs followed)
    };
    enum class LeveledSpawnMatch { kNone, kOriginalBase, kLeaf, kOnlyNew };
    struct LeveledSpawnChoice {
        std::size_t index = 0;           // into the candidates; meaningful unless match is kNone
        LeveledSpawnMatch match = LeveledSpawnMatch::kNone;
        std::size_t matching = 0;        // candidates the chosen rule matched (more than 1: ambiguous, the first is taken)
    };
    // The first candidate whose originalBase is the sample; else the first whose template base is one of its entries; else
    // the only candidate when there is exactly one. kNone with no candidate, or with several of which none matches.
    LeveledSpawnChoice ChooseLeveledSpawn(const std::vector<LeveledSpawnCandidate>& candidates);
    const char* LeveledSpawnMatchName(LeveledSpawnMatch m);
    // The test's last line. DONE (what a watcher reads as "the run finished") only for a test that spawned its samples;
    // one that ended before its spawn checked nothing and runs again at the next save load in the session: RESTART with
    // the reason ("ended before the spawn" when empty).
    // notRun: runs that never started and have no RESULT line: a sample that stopped in run k of N (a failure or its plugin not
    // loaded, before a spawn) or a test that ended in it adds N-k (run k has its RESULT); added to DONE as notRun=N when not 0.
    std::string SelfTestEndLine(bool spawned, std::string_view reason, std::size_t pass, std::size_t fail, std::size_t inconclusive, std::size_t notRun = 0);
}
