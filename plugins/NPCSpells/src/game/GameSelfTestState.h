#pragma once

// =============================================================================
// NPC SPELLS - DEV SELF TEST: SHARED STATE (internal to GameSelfTest*.cpp)
// =============================================================================
// GameSelfTest.cpp runs the test (ticks, spawn, cleanup, load and save
// messages); GameSelfTestCheck.cpp checks one spawned actor. Every member is
// touched on the game's main thread (SKSE tasks and messages) except the
// atomics, which the worker thread and the save message read.
// =============================================================================

#include "core/NpcSpellsRules.h"
#include "core/NpcSpellsSelfTest.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NpcSpellsGame::SelfTest
{
    // Our ESL's records, told apart by their file and local FormID (GetFormEditorID() is empty without
    // powerofthree's Tweaks, GameNpcs.cpp). The local id gives slot and chunk: LocalFormId (core/NpcSpellsSlots.h).
    struct OurLists
    {
        const RE::TESFile* file = nullptr;

        std::optional<std::pair<std::size_t, std::uint8_t>> Find(const RE::TESLevSpell* lev) const;
    };

    struct SampleRun
    {
        std::string text;                      // the sample as the config writes it
        std::string label;                     // the per-run lines' name for it (NpcSpells::SelfTestRunLabel)
        int run = 1, runs = 1;                 // this spawn of the sample, of how many (config "repeat"), one after another
        NpcSpells::SelfTestRunTally tally;     // a repeated sample's checked runs, for its final SELFTEST lists line
        NpcSpells::FormRef ref;
        RE::TESBoundObject* placed = nullptr;  // what is spawned: the sample's TESNPC or TESLevCharacter
        RE::TESNPC* npc = nullptr;             // the sample as an NPC; null for a leveled sample
        RE::TESNPC* holder = nullptr;          // its spell list holder; for a leveled or unresolvable sample found after the spawn
        RE::TESNPC* holderStart = nullptr;     // the record the holder's template walk started from
        const char* holderFrom = "none";       // which record that was: record, templateBase, originalBase, actorBase
        bool unresolvable = false;             // the template chain has a leveled link before the holder
        bool resolvedAtPrepare = false;        // holder found from the record (Prepare); otherwise each run finds it after its spawn
        RE::ObjectRefHandle handle;            // a handle, not a NiPointer: the state may be freed on the worker thread
        RE::FormID spawnedId = 0;              // the spawned ref's FormID: cleanup touches only a ref that still has it
        std::size_t waitTicks = 0;             // ticks since the spawn tick
        bool done = false;                     // this run has its RESULT
        bool stopped = false;                  // no further run: a failure (or a plugin not loaded) before the spawn would end every run the same way
        bool spawnPending = false;             // a leveled sample's placeatme ran and its actor is not found yet (FindLeveledSpawn)
        bool onlyNew = false;                  // this run's actor was matched only as the one new actor (after the spawn wait): not checked, left in the world
        std::unordered_set<RE::FormID> actorsBefore;  // the loaded actors just before that placeatme
        std::unordered_set<RE::FormID> entries;       // the leveled sample's NPC entries (nested lists followed), read at each run's spawn
    };

    struct State
    {
        NpcSpells::SelfTestConfig config;
        std::vector<SampleRun> samples;
        OurLists ours;
        bool spawned = false;
        bool aborted = false;                  // the world the samples stood in is gone: the next tick ends the test
        std::string abortReason;               // for the open samples' FAIL line
        std::size_t notReadyTicks = 0;         // ticks without the player's 3D and cell (the player wait)
        std::size_t readyTicks = 0;            // ticks with them since they last came (the start delay)
        std::size_t pass = 0, fail = 0, inconclusive = 0;
        std::vector<std::string> lines;        // appended to the report at the end
        std::unordered_set<RE::FormID> spawnedIds;  // every ref this test spawned, matched or counted as left: never a later leveled spawn's candidate
        std::atomic<int> liveActors{ 0 };      // spawned and not yet cleaned up (read by the save message)
        std::atomic<int> pendingSpawns{ 0 };   // leveled placeatme runs whose actor is not found yet (read by the save message)
        std::atomic<int> leftActors{ 0 };      // actors the test leaves in the world: a leveled placeatme's actor not found in time, kept as the only new one, or the other matches of an ambiguous pick (FindLeveledSpawn, LeveledSpawnMissing)
        std::atomic<bool> savedWithActors{ false };
        std::atomic<bool> queued{ false };     // a Tick is queued and has not finished: the worker queues at most one
        std::atomic<bool> finished{ false };
    };

    // Each line goes to the log at its level and to the report's "== Self test ==" section
    void Log(State& s, std::string line);
    void Warn(State& s, std::string line);
    void Error(State& s, std::string line);
    void Fail(State& s, SampleRun& r, std::string_view reason);
    // RESULT line and the count of its verdict; marks the sample done
    void Record(State& s, SampleRun& r, NpcSpells::SelfTestVerdict verdict, std::string_view reason);
    std::string FormText(const RE::TESForm* form);
    std::vector<RE::TESLevSpell*> OurListsOn(const RE::TESSpellList::SpellData* d, const OurLists& ours);

    // A leveled sample's spawn (GameSelfTestSpawn.cpp). TESObjectREFR::PlaceObjectAtMe gives no actor for a TESLevCharacter
    // (in-game run 6); the console's placeatme does (run 5), so the command runs as a console script on the player. It
    // returns no ref: PlaceLeveled notes the loaded actors first, runs the command and looks for the new actor at once
    // (FindLeveledSpawn, before any other sample's spawn); not found, it sets spawnPending and the ticks look again.
    // false + error: the command could not be run.
    bool PlaceLeveled(State& s, SampleRun& r, RE::PlayerCharacter* player, std::string& error);
    // The new actor (NpcSpells::ChooseLeveledSpawn) and its spawn line; kNone: not found (yet). waitOver: the spawn wait
    // is over, so a lone new actor that matches no rule is taken (kOnlyNew); before that it is not. A real match sets the
    // handle (other actors matching the same rule are left in the world, WARN line); kOnlyNew sets onlyNew instead (the
    // actor is not checked and is left in the world, WARN line).
    NpcSpells::LeveledSpawnMatch FindLeveledSpawn(State& s, SampleRun& r, std::size_t waitedTicks, bool waitOver);
    // The FAIL reason when no actor was found within maxWaitMs (also logs the new actors at any distance, up to five, and
    // counts those within reach as left in the world, at least 1: NpcSpells::LeveledSpawnLeftCount)
    std::string LeveledSpawnMissing(State& s, SampleRun& r, std::size_t waitedTicks);

    // The placeatme's actor is no longer looked for
    inline void ClearSpawnPending(State& s, SampleRun& r)
    {
        if (r.spawnPending) {
            r.spawnPending = false;
            --s.pendingSpawns;
        }
        r.actorsBefore.clear();
    }

    // One actor with its 3D and high process: logs its bases, holder, leveled spell cache and lists, adds the run to the
    // sample's tally, then the verdict
    void Check(State& s, SampleRun& r, RE::Actor* actor, std::size_t waitedTicks);
}
