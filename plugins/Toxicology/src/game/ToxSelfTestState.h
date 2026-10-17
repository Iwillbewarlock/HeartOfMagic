#pragma once

// =============================================================================
// Toxicology - dev self test: shared state (internal to ToxSelfTest*.cpp)
// =============================================================================
// ToxSelfTest.cpp       the run: file gate, ticks, load/save messages, undo, DONE
// ToxSelfTestSetup.cpp  step 1, the poison / spell / target picks, step 5,
//                       and the target, effect and inventory helpers
// ToxSelfTestHits.cpp   steps 2, 3 and 6 (poison spell hits on spawned targets)
// ToxSelfTestDoses.cpp  step 7 (Magicka spent -> bottles)
// ToxSelfTestReload.cpp the reload check of step 7 (its state file)
// ToxSelfTestShop.cpp   the tome in Elgrim's Elixirs (spec 4a, read only)
// Every member is touched on the game's main thread (SKSE tasks and messages)
// except the atomics, which the worker thread reads.
// =============================================================================

#include "game/ToxApply.h"
#include "game/ToxHits.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ToxGame::SelfTest
{
    // =========================================================================
    // Constants
    // =========================================================================

    inline constexpr int kPollMs = 100;             // one main-thread tick per this much
    inline constexpr int kPlayerWaitMs = 60000;     // the player's 3D and cell, before the start delay
    inline constexpr int kPlayerGoneMs = 15000;     // after step 1 began: gone this long (loading screens not counted) ends the test
    inline constexpr int kSettleTicks = 3;          // ticks after the first sign of a hit before it is judged
    // A test poison's longest effect: at least this long, so it is still on the target at step 3's second hit
    inline constexpr float kMinPoisonSeconds = 8.0f;
    inline constexpr float kMagickaTolerance = 0.01f;

    // =========================================================================
    // Config (the dev file)
    // =========================================================================

    struct Config
    {
        int startDelayMs = 30000;   // after the player stands loaded (the load settles first)
        int spawnWaitMs = 10000;    // a target's 3D and high process
        int hitWaitMs = 4000;       // a cast's hit task, from the cast
        float targetDistance = 120.0f;  // game units in front of the player (touch reach)
        float maxPoisonSeconds = 30.0f; // a test poison's longest effect: at most this long
        bool disableTargetAI = false;   // true: Actor::EnableAI(false) on targets (may stop their effect timers)
        bool reloadCheck = true;        // step 7's save + load check
        std::string targetBase = "0510C2:Skyrim.esm";  // CWMission08CowHand: no factions, no crime faction, no spells, no script
    };

    // =========================================================================
    // State
    // =========================================================================

    enum class Step
    {
        kWaitPlayer,
        kSetup,       // step 1 (+ the picks, step 5)
        kDeliveries,  // steps 2 and 6
        kSecondHit,   // step 3
        kDoses,       // step 7
    };

    struct Delivery
    {
        std::string label;                 // "aimed", "touch", "target actor", "target location", "concentration", "cloak"
        RE::SpellItem* spell = nullptr;    // what is cast (the cloak: the cloak spell, cast on the player)
        RE::SpellItem* hitSpell = nullptr; // what hits the target (the cloak: its sub-spell)
        bool cloak = false;
        bool landed = false;               // step 2: our cast put the poison on the target (kCast)
    };

    struct Target
    {
        RE::ActorHandle handle;
        RE::FormID id = 0;
        RE::NiPoint3 placedAt;
        std::size_t waited = 0;
    };

    // An effect seen in a list: the address is only an identity, used after it is found again
    struct EffectRef
    {
        const RE::ActiveEffect* effect = nullptr;
        std::uint16_t uniqueId = 0;
    };

    struct State
    {
        Config config;
        Step step = Step::kWaitPlayer;
        int phase = 0;
        std::size_t phaseTicks = 0;      // ticks since the phase began
        std::size_t notReadyTicks = 0;   // ticks without the player's 3D and cell
        std::size_t readyTicks = 0;      // ticks with them (the start delay)
        std::size_t playerGoneTicks = 0; // after step 1 began: ticks without them
        bool begun = false;              // step 1 started changing the world
        bool aborted = false;            // another save or a new game: the next tick ends the test
        std::string abortReason;
        std::size_t pass = 0;
        std::size_t fail = 0;

        // What the test changed (undone at the end)
        bool addedAbility = false;
        RE::AlchemyItem* poisonA = nullptr;  // the name-first test poison (step 1's expected choice)
        RE::AlchemyItem* poisonB = nullptr;
        float originalAccumulated = 0.0f;
        bool dosesRan = false;
        RE::SpellItem* cloakOnPlayer = nullptr;
        RE::TESNPC* targetBase = nullptr;
        Target target;                   // the one live spawned target (id 0 = none)

        // Steps 2-3
        std::vector<Delivery> deliveries;
        std::size_t deliveryIndex = 0;
        std::optional<std::size_t> secondHitDelivery;  // step 3 uses this delivery
        HitTrace traceBefore;
        std::optional<std::size_t> hitSeenTick;
        bool castSync = false;           // the cast spell's own effects were on the target right after the call
        std::vector<EffectRef> playerBefore;
        std::vector<EffectRef> owned;    // step 3: the poison's effects on the target after the first hit
        std::int32_t bottlesAtHits = 0;
        // The count toward the next bottle is set to 0 before each delivery cast (steps 2-3), so a cast
        // the addon charged cannot use up poison A; what each cast left is added here first
        bool countZeroed = false;
        float chargedByTestCasts = 0.0f;

        std::atomic<bool> queued{ false };
        std::atomic<bool> finished{ false };
    };

    // =========================================================================
    // Log (ToxSelfTest.cpp)
    // =========================================================================

    void Pass(State& s, std::string_view step, std::string_view detail);
    void Fail(State& s, std::string_view step, std::string_view detail);
    void Check(State& s, std::string_view step, bool ok, std::string_view detail);  // PASS when ok, else FAIL
    void Info(std::string_view what, std::string_view detail);
    bool WaitOver(std::size_t a_ticks, double a_ms);
    void NextPhase(State& s, int a_phase);
    void NextStep(State& s, Step a_step);
    const char* OutcomeName(std::optional<ApplyOutcome> a_outcome);
    const char* StageName(HitStage a_stage);
    std::string FormText(const RE::TESForm* a_form);

    // =========================================================================
    // Setup and helpers (ToxSelfTestSetup.cpp)
    // =========================================================================

    // Step 1 and the picks; false: nothing further can be tested (FAIL logged)
    bool Setup(State& s);
    void StepI5();
    void StepShop(State& s);  // ToxSelfTestShop.cpp (spec 4a, read only)
    void StepDoses(State& s);  // ToxSelfTestDoses.cpp

    bool SpawnTarget(State& s, std::string_view step);
    enum class Readiness { kWaiting, kReady, kTimedOut };
    Readiness TargetReady(State& s);
    RE::NiPointer<RE::Actor> TargetActor(State& s);
    void DeleteTarget(State& s);
    void CalmTarget(State& s);
    void DispelCloak(State& s);
    std::string TargetText(State& s, RE::Actor* a_actor);

    // The poison's effects in the actor's list that are not kInactive or kDispelled
    std::vector<RE::ActiveEffect*> LiveEffectsOf(RE::Actor* a_actor, const RE::MagicItem* a_item);
    std::vector<EffectRef> Refs(const std::vector<RE::ActiveEffect*>& a_effects);
    // a_ref found again in the actor's list (same address and usUniqueID), or null
    RE::ActiveEffect* FindAgain(RE::Actor* a_actor, const EffectRef& a_ref);
    bool IsLiveEffect(const RE::ActiveEffect& a_effect);
    // CastSpellImmediate from the player's instant caster at a_target, the player blamed (ToxApply's call)
    bool CastFromPlayer(RE::MagicItem* a_item, RE::TESObjectREFR* a_target);

    std::int32_t BottleCount(RE::AlchemyItem* a_poison);
    void RemoveAllBottles(RE::AlchemyItem* a_poison);
    // All bottles removed, then a_count added and favorited (checked as entry IsFavorited and an ExtraHotkey list)
    bool GiveFavorited(RE::AlchemyItem* a_poison, std::int32_t a_count, std::string& a_why);
    // HoMTox_MimicPoison on a_on (when showIcon) and not on a_off
    void CheckKeyword(State& s, std::string_view a_step, RE::AlchemyItem* a_on, RE::AlchemyItem* a_off);

    // =========================================================================
    // Hits (ToxSelfTestHits.cpp)
    // =========================================================================

    void TickDeliveries(State& s);
    void TickSecondHit(State& s);

    // =========================================================================
    // Reload check (ToxSelfTestReload.cpp)
    // =========================================================================

    // Writes the state file: the test count to find after a save + load, and the count to put back
    bool ArmReloadCheck(float a_expected, float a_original);
    bool ReloadCheckPending();
    // kPostLoadGame with the state file present: checks or keeps waiting; logs its own DONE
    void RunReloadCheck();
    void NoteReloadSave(std::string_view a_saveName);
    void NoteReloadLoading(std::string_view a_saveName);
}
