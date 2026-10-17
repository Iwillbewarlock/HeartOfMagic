#include "game/ToxSelfTest.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxMagicka.h"
#include "game/ToxSelection.h"
#include "game/ToxSelfTestState.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;
using SelfTest::Step;

// =============================================================================
// Shared helpers (ToxSelfTestState.h)
// =============================================================================

void SelfTest::Pass(State& s, std::string_view step, std::string_view detail)
{
    ++s.pass;
    logger::info("SELFTEST PASS {}: {}", step, detail);
}

void SelfTest::Fail(State& s, std::string_view step, std::string_view detail)
{
    ++s.fail;
    logger::warn("SELFTEST FAIL {}: {}", step, detail);
}

void SelfTest::Check(State& s, std::string_view step, bool ok, std::string_view detail)
{
    ok ? Pass(s, step, detail) : Fail(s, step, detail);
}

void SelfTest::Info(std::string_view what, std::string_view detail)
{
    logger::info("SELFTEST INFO {}: {}", what, detail);
}

bool SelfTest::WaitOver(std::size_t a_ticks, double a_ms)
{
    return static_cast<double>(a_ticks) * kPollMs >= a_ms;
}

void SelfTest::NextPhase(State& s, int a_phase)
{
    s.phase = a_phase;
    s.phaseTicks = 0;
    s.hitSeenTick.reset();
}

void SelfTest::NextStep(State& s, Step a_step)
{
    s.step = a_step;
    NextPhase(s, 0);
}

const char* SelfTest::OutcomeName(std::optional<ApplyOutcome> a_outcome)
{
    if (!a_outcome) return "none";
    switch (*a_outcome) {
    case ApplyOutcome::kImmune:
        return "kImmune";
    case ApplyOutcome::kCast:
        return "kCast";
    case ApplyOutcome::kCastNoEffect:
        return "kCastNoEffect";
    case ApplyOutcome::kNoCaster:
        return "kNoCaster";
    }
    return "?";
}

const char* SelfTest::StageName(HitStage a_stage)
{
    switch (a_stage) {
    case HitStage::kNone:
        return "no task";
    case HitStage::kOff:
        return "disabled or no ability";
    case HitStage::kNotPoisonSpell:
        return "not in the poison set";
    case HitStage::kTargetGone:
        return "target gone, dead or the player";
    case HitStage::kSameHit:
        return "repeat of the same hit";
    case HitStage::kNoPoison:
        return "no poison to copy";
    case HitStage::kApplied:
        return "applied";
    }
    return "?";
}

std::string SelfTest::FormText(const RE::TESForm* a_form)
{
    if (!a_form) return "none";
    const char* name = a_form->GetName();  // RE/T/TESForm.h:308
    return std::format("'{}' [{:08X}]", name ? name : "", a_form->GetFormID());
}

// =============================================================================
// The run: ticks on the main thread, queued one at a time by a worker thread
// =============================================================================
// The NPC Spells self test's shape (plugins/NPCSpells/src/game/GameSelfTest.cpp):
// a worker thread queues one Tick per kPollMs and only when the last one has run,
// so every wait counts executed ticks (a game in the background uses none up).
// Game objects are touched only inside the tasks, on the main thread.

namespace
{
    // Step 7's test count: half a dose plus a fraction no real count is likely to sit on, so the
    // reload check can tell the saved test value from a count left over
    constexpr float kReloadTestDoseFraction = 0.5f;
    constexpr float kReloadTestOffset = 0.25f;

    std::mutex g_currentLock;
    std::weak_ptr<State> g_current;  // the running test, for the load and save messages
    bool g_started = false;          // main thread only (SKSE messages and tasks)

    std::shared_ptr<State> Current()
    {
        std::lock_guard lock(g_currentLock);
        return g_current.lock();
    }

    template <class T>
    void ReadValue(const nlohmann::json& a_json, const char* a_key, T& a_out, std::vector<std::string>& a_warnings)
    {
        const auto it = a_json.find(a_key);
        if (it == a_json.end()) return;
        try {
            a_out = it->get<T>();
        } catch (const nlohmann::json::exception&) {
            a_warnings.push_back(std::format("{} = {} has the wrong type, default kept", a_key, it->dump()));
        }
    }

    SelfTest::Config ParseConfig(const nlohmann::json& a_json, std::vector<std::string>& a_warnings)
    {
        SelfTest::Config c;
        if (!a_json.is_object()) {
            a_warnings.emplace_back("the file is not a JSON object, defaults used");
            return c;
        }
        ReadValue(a_json, "startDelayMs", c.startDelayMs, a_warnings);
        ReadValue(a_json, "spawnWaitMs", c.spawnWaitMs, a_warnings);
        ReadValue(a_json, "hitWaitMs", c.hitWaitMs, a_warnings);
        ReadValue(a_json, "targetDistance", c.targetDistance, a_warnings);
        ReadValue(a_json, "maxPoisonSeconds", c.maxPoisonSeconds, a_warnings);
        ReadValue(a_json, "disableTargetAI", c.disableTargetAI, a_warnings);
        ReadValue(a_json, "reloadCheck", c.reloadCheck, a_warnings);
        ReadValue(a_json, "targetBase", c.targetBase, a_warnings);
        const SelfTest::Config defaults;
        if (c.startDelayMs < 0) c.startDelayMs = defaults.startDelayMs;
        if (c.spawnWaitMs <= 0) c.spawnWaitMs = defaults.spawnWaitMs;
        if (c.hitWaitMs <= 0) c.hitWaitMs = defaults.hitWaitMs;
        if (!(c.targetDistance > 0.0f)) c.targetDistance = defaults.targetDistance;
        if (!(c.maxPoisonSeconds >= SelfTest::kMinPoisonSeconds)) c.maxPoisonSeconds = defaults.maxPoisonSeconds;
        return c;
    }

    // The test's changes taken back: target, cloak, trace, bottles, ability, selection, Magicka count.
    // Step 7 leaves the test count in place for the reload check (ArmReloadCheck).
    void Undo(State& s)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        SelfTest::DeleteTarget(s);
        SelfTest::DispelCloak(s);
        for (auto* poison : { s.poisonA, s.poisonB }) {  // a test poison that landed on the player (Self delivery)
            if (!poison) continue;  // Setup stopped before picking it
            for (auto* effect : SelfTest::LiveEffectsOf(player, poison)) effect->Dispel(true);  // RE/A/ActiveEffect.h:92
        }
        ToxGame::SetHitTracing(false);
        SelfTest::RemoveAllBottles(s.poisonA);
        SelfTest::RemoveAllBottles(s.poisonB);
        if (s.addedAbility && player && ToxGame::Ability()) {
            player->RemoveSpell(ToxGame::Ability());  // RE/A/Actor.h:678
        }
        ToxGame::OnSelectionMaybeChanged();  // the keyword back on the player's own choice (or on none)
        const auto* choice = ToxGame::CurrentPoison(true);
        SelfTest::Info("undo", std::format("test poisons left: {} {}, {} {}; ability {} (it was {}); copied poison now {}",
            SelfTest::FormText(s.poisonA), SelfTest::BottleCount(s.poisonA), SelfTest::FormText(s.poisonB),
            SelfTest::BottleCount(s.poisonB), ToxGame::PlayerHasAbility() ? "kept" : "not held",
            s.addedAbility ? "added by the test" : "already held", SelfTest::FormText(choice)));

        const float testValue = ToxGame::GetSettings().magickaPerDose * kReloadTestDoseFraction + kReloadTestOffset;
        if (s.dosesRan && s.config.reloadCheck && SelfTest::ArmReloadCheck(testValue, s.originalAccumulated)) {
            ToxGame::SetAccumulated(testValue);
            SelfTest::Info("7 reload", std::format("ACTION: save the game now (any slot), then load that save. The count toward "
                "the next bottle is {:.2f} (the test value) until then; the check puts the pre-test {:.2f} back",
                testValue, s.originalAccumulated));
        } else {
            ToxGame::SetAccumulated(s.originalAccumulated);
            SelfTest::Info("undo", std::format("count toward the next bottle back to {:.2f}", s.originalAccumulated));
        }
    }

    void Finish(State& s, std::string_view a_note)
    {
        if (s.finished) return;
        if (!s.begun) {
            g_started = false;  // nothing was tested: a later load may start it again
            logger::warn("SELFTEST RESTART ({})", a_note);
        } else {
            logger::info("SELFTEST DONE pass={} fail={}{}{}", s.pass, s.fail, a_note.empty() ? "" : " ", a_note);
        }
        FlushLog();
        s.finished = true;
    }

    void FinishNormally(State& s)
    {
        Undo(s);
        Finish(s, ToxGame::SelfTest::ReloadCheckPending() ? "(reload check pending: save, then load that save)" : "");
    }

    void TickWaitPlayer(State& s)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();  // RE/P/PlayerCharacter.h
        if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {  // RE/T/TESObjectREFR.h:448, :420
            s.readyTicks = 0;
            if (SelfTest::WaitOver(++s.notReadyTicks, SelfTest::kPlayerWaitMs)) {
                Finish(s, "the player's 3D or cell never came");
            }
            return;
        }
        if (SelfTest::WaitOver(s.readyTicks++, s.config.startDelayMs)) {
            SelfTest::NextStep(s, Step::kSetup);
        }
    }

    // After step 1 began: the player's 3D and cell. Missing for kPlayerGoneMs (quit to the main menu, a long
    // gap outside a loading screen) ends the test: in the main menu nothing is touched (the world is gone),
    // otherwise it is undone.
    // A shorter gap, or any time while the loading screen is open, only pauses the steps.
    bool PlayerStillHere(State& s)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->Is3DLoaded() && player->GetParentCell()) {  // RE/T/TESObjectREFR.h:448, :420
            s.playerGoneTicks = 0;
            return true;
        }
        auto* ui = RE::UI::GetSingleton();
        // A loading screen pauses the steps without counting (RE/L/LoadingMenu.h:21): a long one is no abort
        if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) return false;
        if (!SelfTest::WaitOver(++s.playerGoneTicks, SelfTest::kPlayerGoneMs)) return false;
        const bool mainMenu = ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);  // RE/U/UI.h:92, RE/M/MainMenu.h:30
        SelfTest::Fail(s, "abort", std::format("the player's 3D or cell has been gone for {} ms ({}){}", SelfTest::kPlayerGoneMs,
            mainMenu ? "main menu" : "no main menu", mainMenu ? ": nothing touched" : ": the test's changes are undone"));
        if (!mainMenu) Undo(s);
        ToxGame::SetHitTracing(false);
        Finish(s, "(ended: the player left the world)");
        return false;
    }

    void Tick(State& s)
    {
        if (s.aborted) {
            // The world the test changed is gone with the load or new game: nothing is touched
            ToxGame::SetHitTracing(false);
            if (s.begun) SelfTest::Fail(s, "abort", s.abortReason);
            Finish(s, s.abortReason);
            return;
        }
        if (s.begun && !PlayerStillHere(s)) return;
        ++s.phaseTicks;
        switch (s.step) {
        case Step::kWaitPlayer:
            TickWaitPlayer(s);
            return;
        case Step::kSetup:
            s.begun = true;
            if (!SelfTest::Setup(s)) {
                FinishNormally(s);
                return;
            }
            if (s.deliveries.empty() || !s.targetBase) {
                SelfTest::NextStep(s, Step::kDoses);
                return;
            }
            s.bottlesAtHits = SelfTest::BottleCount(s.poisonA);
            ToxGame::SetHitTracing(true);
            SelfTest::NextStep(s, Step::kDeliveries);
            return;
        case Step::kDeliveries:
            SelfTest::CalmTarget(s);
            SelfTest::TickDeliveries(s);
            return;
        case Step::kSecondHit:
            SelfTest::CalmTarget(s);
            SelfTest::TickSecondHit(s);
            return;
        case Step::kDoses:
            SelfTest::StepDoses(s);
            FinishNormally(s);
            return;
        }
    }

    // Nothing leaves an SKSE task: an error undoes what it can and ends the test
    void StopOnError(State& s, std::string_view a_what) noexcept
    {
        try {
            logger::error("SELFTEST stopped by an error: {}", a_what);
            if (s.begun) {
                ++s.fail;
                Undo(s);
            }
            Finish(s, std::format("(stopped by an error: {})", a_what));
        } catch (...) {
        }
        s.finished = true;
    }

    void RunTick(State& s) noexcept
    {
        if (s.finished) return;
        try {
            Tick(s);
        } catch (const std::exception& e) {
            StopOnError(s, e.what());
        } catch (...) {
            StopOnError(s, "unknown error");
        }
    }

    void Start()
    {
        std::ifstream file(ToxGame::kSelfTestPath, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        const auto json = nlohmann::json::parse(text.str(), nullptr, false);
        auto state = std::make_shared<State>();
        std::vector<std::string> warnings;
        if (json.is_discarded()) {
            warnings.emplace_back("not valid JSON, defaults used");
        } else {
            state->config = ParseConfig(json, warnings);
        }
        for (const auto& w : warnings) {
            logger::warn("SELFTEST config: {}", w);
        }
        const auto& c = state->config;
        logger::info("SELFTEST start startDelayMs={} spawnWaitMs={} hitWaitMs={} targetDistance={} maxPoisonSeconds={} "
            "disableTargetAI={} reloadCheck={} targetBase={} pollMs={}", c.startDelayMs, c.spawnWaitMs, c.hitWaitMs,
            c.targetDistance, c.maxPoisonSeconds, c.disableTargetAI, c.reloadCheck, c.targetBase, SelfTest::kPollMs);
        {
            std::lock_guard lock(g_currentLock);
            g_current = state;
        }
        // Runs until the test finishes: every wait is a finite number of ticks. A tick that never runs
        // (the game quit) leaves this thread asleep in its loop until the process ends.
        std::thread([state] {
            while (!state->finished) {
                try {
                    std::this_thread::sleep_for(std::chrono::milliseconds(SelfTest::kPollMs));
                    if (state->queued.exchange(true)) continue;  // the last tick has not run yet
                    const auto* tasks = SKSE::GetTaskInterface();  // SKSE/API.h
                    if (!tasks) {
                        state->queued = false;
                        continue;
                    }
                    tasks->AddTask([state] {  // SKSE/Interfaces.h:394, runs on the main thread
                        RunTick(*state);
                        state->queued = false;
                    });
                } catch (...) {
                    state->queued = false;
                }
            }
        }).detach();
    }

    bool DevFilePresent()
    {
        std::error_code ec;
        return std::filesystem::exists(ToxGame::kSelfTestPath, ec);
    }

    // The save name SKSE passes with kPreLoadGame / kSaveGame (skse64's convention; CommonLibSSE-NG's
    // SKSE/Interfaces.h does not document the data): up to a_length bytes, empty when none
    std::string_view SaveNameView(const char* a_name, std::uint32_t a_length)
    {
        if (!a_name || a_length == 0) return {};
        const auto* end = std::find(a_name, a_name + a_length, '\0');
        return { a_name, static_cast<std::size_t>(end - a_name) };
    }
}

// =============================================================================
// Public
// =============================================================================

void ToxGame::MaybeStartSelfTest()
{
    try {
        // The shipped path: no dev file, one file-exists check, nothing else (the reload check never
        // touches the co-saved count without it)
        if (!DevFilePresent()) return;
        if (SelfTest::ReloadCheckPending()) {
            g_started = true;  // no main test while a reload check waits (this session)
            SelfTest::RunReloadCheck();
            FlushLog();
            return;
        }
        if (g_started) return;
        g_started = true;
        Start();
    } catch (const std::exception& e) {
        logger::error("SELFTEST DONE pass=0 fail=1 (not started: {})", e.what());
    } catch (...) {
        logger::error("SELFTEST DONE pass=0 fail=1 (not started: unknown error)");
    }
}

void ToxGame::OnSelfTestPreLoad(const char* a_saveName, std::uint32_t a_length)
{
    // A running test first (it exists only with the dev file; no file check needed for it)
    if (const auto s = Current(); s && !s->finished) {
        if (!s->begun) {
            s->notReadyTicks = 0;
            s->readyTicks = 0;
            logger::info("SELFTEST another save is loading before the test began: the wait starts again");
        } else {
            s->aborted = true;
            s->abortReason = "another save was loaded during the test";
            logger::warn("SELFTEST WARN another save is loading during the test: the next tick ends it (its changes go with the old world)");
        }
    }
    try {
        if (DevFilePresent()) SelfTest::NoteReloadLoading(SaveNameView(a_saveName, a_length));
    } catch (const std::exception& e) {
        logger::error("SELFTEST could not note the loading save: {}", e.what());
    }
}

void ToxGame::OnSelfTestNewGame()
{
    const auto s = Current();
    if (!s || s->finished) return;
    s->aborted = true;
    s->abortReason = "a new game was started during the test";
    logger::warn("SELFTEST WARN a new game was started during the test: the next tick ends it");
}

void ToxGame::OnSelfTestSave(const char* a_saveName, std::uint32_t a_length)
{
    const auto name = SaveNameView(a_saveName, a_length);
    if (const auto s = Current(); s && !s->finished && s->begun) {
        logger::warn("SELFTEST WARN save '{}' taken during the test: it holds the test's ability, poisons and target, and "
            "loading it later brings that test state back", name);
        return;
    }
    try {
        if (DevFilePresent()) SelfTest::NoteReloadSave(name);
    } catch (const std::exception& e) {
        logger::error("SELFTEST could not note the save for the reload check: {}", e.what());
    }
}
