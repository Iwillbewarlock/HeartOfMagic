#pragma once

// =============================================================================
// Toxicology - the tome as the reward of Few and Far Between
// =============================================================================
// Spec 4a: the tome is Ingun Black-Briar's reward for the vanilla quest
// FreeformRiften04 "Few and Far Between" (0x053308, Skyrim.esm). Its stage 200
// is the success stage (Fragment_5: the key, the thane count, Ingun's
// relationship, CompleteAllObjectives, Stop()); 250 is the failure. Stage 200's
// log entry has no CompleteQuest flag, so TESQuest::IsCompleted() is not a
// success test: the current stage is.
//
// Two ways in, both ending in TryGrantTome on the main thread:
//   - kQuestStage: a TESQuestStageEvent sink sees the quest reach stage 200 and
//     queues an SKSE task;
//   - kAlreadyFinished: after a successful save load (kPostLoadGame) a task
//     reads the quest's current stage; at 200 (a save that finished the quest
//     before the addon was installed, or while "enabled" was off) it tries too.
//     Known limit: a later stage set by a mod or the console (250 after 200)
//     hides a finished quest from this check.
// Both tasks carry the session generation (ToxHits.h) and are dropped after a
// load or a new game. TryGrantTome reads every input inside the task (the flag,
// the ability, the tomes held), so two tasks in one frame cannot both grant.
// The decision is core DecideTomeReward (checked by tools/toxicology-test).
// Once the reward is satisfied - the tome given, or the player already knew the
// ability or held a tome - it is never given again: the flag is kept in the
// co-save (ToxSave.h, record TOMG).
//
// Threads: the sink may run on any thread (logged once); it reads the event's
// two fields and an atomic, and queues the task. The flag is an atomic (the
// co-save callbacks' thread is logged, not guaranteed); everything else runs on
// the game's main thread.
// =============================================================================

#include "core/ToxCore.h"

#include <cstdint>

namespace ToxGame
{
    inline constexpr RE::FormID kRewardQuestId = 0x053308;  // FreeformRiften04, Skyrim.esm
    inline constexpr std::uint16_t kRewardStage = 200;

    enum class RewardPath
    {
        kQuestStage,       // the stage-200 event
        kAlreadyFinished,  // the stage read after a save load
    };

    const char* RewardPathName(RewardPath a_path);

    // Looks the quest up (null: the reward is off, logged once as an error). Call once at kDataLoaded.
    void LoadRewardQuest();

    // The quest LoadRewardQuest found, or null
    RE::TESQuest* RewardQuest();

    // Registers the TESQuestStageEvent sink. Call once at kDataLoaded, after LoadRewardQuest.
    void RegisterQuestStageSink();

    // kPostLoadGame after a successful load only: queues the task that reads the quest's stage and,
    // at kRewardStage, calls TryGrantTome(kAlreadyFinished).
    void QueueFinishedQuestCheck();

    // Main thread. Reads the inputs, decides (core DecideTomeReward), logs the decision with every
    // input and acts: kGrant adds one tome to the player, and only when the count went up sets the
    // flag and shows "<sAddItemtoInventory>: <tome name>" on the HUD; kMarkSatisfied sets the flag.
    // Returns the decision.
    Toxicology::TomeRewardAction TryGrantTome(RewardPath a_path);

    // Main thread. Tomes in the player's inventory (0 without the player or the ESL).
    std::int32_t PlayerTomeCount();

    // The co-saved flag (any thread)
    bool RewardSatisfied();
    void SetRewardSatisfied(bool a_satisfied);
}
