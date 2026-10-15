#include "game/GameSelfTest.h"

#include "Common.h"

#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsSelfTest.h"
#include "core/NpcSpellsSlots.h"
#include "game/GameFiles.h"
#include "game/GameNpcs.h"
#include "game/GamePool.h"
#include "game/GameSelfTestState.h"
#include "PluginNames.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace SelfTest = NpcSpellsGame::SelfTest;
using SelfTest::SampleRun;
using SelfTest::State;
using SelfTest::Fail;
using SelfTest::FormText;
using SelfTest::OurListsOn;

// =============================================================================
// Shared helpers (GameSelfTestState.h)
// =============================================================================

std::optional<std::pair<std::size_t, std::uint8_t>> SelfTest::OurLists::Find(const RE::TESLevSpell* lev) const
{
    if (!file || !lev || lev->GetFile(0) != file) {  // RE/T/TESForm.h:275
        return std::nullopt;
    }
    const auto local = lev->GetLocalFormID();  // RE/T/TESForm.h:295
    if (local < NpcSpells::kFirstLocalFormId) {
        return std::nullopt;
    }
    const auto rel = local - NpcSpells::kFirstLocalFormId;
    const std::size_t slot = rel / NpcSpells::kMaxChunks;
    if (slot >= NpcSpells::SlotTable().size()) {
        return std::nullopt;
    }
    return std::pair{ slot, static_cast<std::uint8_t>(rel % NpcSpells::kMaxChunks) };
}

std::string SelfTest::FormText(const RE::TESForm* form)
{
    if (!form) {
        return "none";
    }
    const auto* file = form->GetFile(0);
    if (!file) {
        return std::format("0x{:08X}", form->GetFormID());  // made at runtime (0xFF...)
    }
    return PluginNames::MakePersistentId(file->fileName, form->GetLocalFormID());
}

std::vector<RE::TESLevSpell*> SelfTest::OurListsOn(const RE::TESSpellList::SpellData* d, const OurLists& ours)
{
    std::vector<RE::TESLevSpell*> out;
    for (std::uint32_t i = 0; d && d->levSpells && i < d->numlevSpells; ++i) {  // RE/T/TESSpellList.h:44-49
        if (ours.Find(d->levSpells[i])) {
            out.push_back(d->levSpells[i]);
        }
    }
    return out;
}

void SelfTest::Log(State& s, std::string line)
{
    logger::info("{}", line);
    s.lines.push_back(std::move(line));
}

void SelfTest::Warn(State& s, std::string line)
{
    logger::warn("{}", line);
    s.lines.push_back(std::move(line));
}

void SelfTest::Error(State& s, std::string line)
{
    logger::error("{}", line);
    s.lines.push_back(std::move(line));
}

void SelfTest::Record(State& s, SampleRun& r, NpcSpells::SelfTestVerdict verdict, std::string_view reason)
{
    if (r.done) {
        return;
    }
    r.done = true;
    ++(verdict == NpcSpells::SelfTestVerdict::kPass ? s.pass : verdict == NpcSpells::SelfTestVerdict::kInconclusive ? s.inconclusive : s.fail);
    Log(s, std::format("SELFTEST RESULT sample {} {} ({})", r.text, NpcSpells::VerdictName(verdict), reason));
}

void SelfTest::Fail(State& s, SampleRun& r, std::string_view reason)
{
    Record(s, r, NpcSpells::SelfTestVerdict::kFail, reason);
}

// =============================================================================
// The run: ticks on the main thread, queued one at a time by a worker thread
// =============================================================================

namespace
{
    // A task that queued itself again might run again in the same drain of SKSE's task queue (not checked in SKSE's
    // source), so the wait is not a self-requeueing task: a worker thread queues one Tick per kSelfTestPollMs, and only
    // when the last one has run (State::queued). Every wait counts executed ticks, so a game window in the background
    // (alt-tab: no task runs) uses up none. The worker only sleeps and queues; every game object is touched inside the tasks, on the main thread.
    // No main menu check: the test starts only on kPostLoadGame, and IsMenuOpen(MainMenu) stayed true for 10 ticks
    // after a load from the main menu (1.6.1170, 2026-10-15), so it ended every test; kPreLoadGame, kNewGame and the
    // finite player wait cover the other cases.

    std::mutex g_currentLock;
    std::weak_ptr<State> g_current;  // the running test, for the load and save messages
    bool g_started = false;          // a test ran (or is running) this session; main thread only (SKSE messages and tasks)

    std::shared_ptr<State> Current()
    {
        std::lock_guard lock(g_currentLock);
        return g_current.lock();
    }

    // The world the ref stood in is gone (another save, a new game): the handle is dropped unread
    void Forget(State& s, SampleRun& r)
    {
        if (!r.handle) {
            return;
        }
        r.handle = {};
        --s.liveActors;
    }

    // RE/T/TESObjectREFR.h:330 Disable, :218 SetDelete. The const get (RE/B/BSPointerHandle.h:161, the const
    // GetSmartPointer) leaves the handle as it is; the non-const one is the engine's "clears the handle" lookup. Only a
    // ref that still has the spawned FormID and is an actor is touched.
    void Cleanup(State& s, SampleRun& r)
    {
        if (!r.handle) {
            return;
        }
        const auto ref = std::as_const(r.handle).get();
        if (ref && ref->GetFormID() == r.spawnedId && ref->As<RE::Actor>()) {
            ref->Disable();
            ref->SetDelete(true);
        }
        Forget(s, r);
    }

    void Spawn(State& s, RE::PlayerCharacter* player)
    {
        NpcSpellsGame::PluginLookup lookup;
        for (auto& r : s.samples) {
            auto* form = NpcSpellsGame::LookupLocalForm(lookup, r.ref.pluginLower, r.ref.local);
            r.npc = form ? form->As<RE::TESNPC>() : nullptr;
            auto* leveled = form ? form->As<RE::TESLevCharacter>() : nullptr;  // RE/T/TESLevCharacter.h:10, a TESBoundObject
            r.placed = r.npc ? static_cast<RE::TESBoundObject*>(r.npc) : leveled;
            if (!r.placed) {
                Fail(s, r, form ? "the record is neither an NPC (TESNPC) nor a leveled NPC (TESLevCharacter)" : "base record not found in this load order");
                continue;
            }
            if (r.npc) {
                auto* holder = NpcSpellsGame::SpellListHolder(r.npc, r.unresolvable);
                if (!r.unresolvable) {  // an unresolvable chain is resolved from the spawned actor (GameSelfTestCheck.cpp)
                    r.holder = holder;
                    r.holderStart = r.npc;
                    r.holderFrom = "record";
                    if (OurListsOn(r.holder->actorEffects, s.ours).empty()) {
                        Fail(s, r, std::format("spell list holder {} has no HoMNS list (is it in the report's NPC lists? is the ESL loaded?)", FormText(r.holder)));
                        continue;
                    }
                }
            }
            // RE/T/TESObjectREFR.h:475: at the player's position, a hostile NPC stands next to the player until it is checked
            const auto ref = player->PlaceObjectAtMe(r.placed, false);
            if (!ref || !ref->As<RE::Actor>()) {
                if (ref) {
                    ref->Disable();
                    ref->SetDelete(true);
                }
                Fail(s, r, "PlaceObjectAtMe gave no actor");
                continue;
            }
            r.handle = ref->GetHandle();  // RE/T/TESObjectREFR.h:402
            r.spawnedId = ref->GetFormID();
            ++s.liveActors;
            Log(s, std::format("SELFTEST spawn sample {} ref 0x{:08X}", r.text, ref->GetFormID()));
        }
        FlushLog();  // info lines are buffered (Common.h): a crash while hostile actors stand here keeps these lines
    }

    void AppendReport(const std::vector<std::string>& lines)
    {
        std::ofstream file(NpcSpellsGame::kReportPath, std::ios::binary | std::ios::app);
        file << "\n== Self test ==\n";
        for (const auto& line : lines) {
            file << line << '\n';
        }
        if (!file) {
            logger::warn("SELFTEST could not append to the report {}", NpcSpellsGame::kReportPath);
        }
    }

    // The last line (NpcSpells::SelfTestEndLine): DONE for a test that spawned; RESTART for one that ended before its
    // spawn, which checked nothing and runs again at a later load in this session (beforeSpawnReason names why)
    void Finish(State& s, std::string_view beforeSpawnReason = {})
    {
        if (s.finished) {
            return;
        }
        for (auto& r : s.samples) {
            Cleanup(s, r);
        }
        if (s.savedWithActors) {
            s.lines.emplace_back("SELFTEST WARN save taken while sample actors exist");  // logged when it happened
        }
        if (!s.spawned) {
            g_started = false;
        }
        SelfTest::Log(s, NpcSpells::SelfTestEndLine(s.spawned, beforeSpawnReason, s.pass, s.fail, s.inconclusive));
        AppendReport(s.lines);
        FlushLog();
        s.finished = true;
    }

    // Ends the test early. After the spawn the open samples FAIL with the reason (touchRefs false: the world they stood
    // in is gone, their handles are dropped unread); before it no sample was checked, so no RESULT line, only RESTART.
    void End(State& s, std::string_view reason, bool touchRefs)
    {
        if (s.spawned) {
            for (auto& r : s.samples) {
                touchRefs ? Cleanup(s, r) : Forget(s, r);
                SelfTest::Fail(s, r, reason);
            }
        }
        Finish(s, reason);
    }

    void Tick(State& s)
    {
        if (s.aborted) {
            End(s, s.abortReason, false);
            return;
        }
        if (!s.spawned) {
            auto* player = RE::PlayerCharacter::GetSingleton();  // RE/P/PlayerCharacter.h:725
            if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {  // RE/T/TESObjectREFR.h:448, :420
                s.readyTicks = 0;  // the start delay counts from the last time the player came in
                if (NpcSpells::SelfTestWaitOver(++s.notReadyTicks, NpcSpells::kSelfTestPlayerWaitMs)) {
                    End(s, "the player's 3D or cell never came", false);
                }
                return;
            }
            if (!NpcSpells::SelfTestWaitOver(s.readyTicks++, s.config.startDelayMs)) {
                return;
            }
            s.spawned = true;
            Spawn(s, player);
        }
        bool open = false;
        for (auto& r : s.samples) {
            if (r.done) {
                continue;
            }
            const auto waited = r.waitTicks++;  // 0 on the spawn tick
            const auto ref = std::as_const(r.handle).get();
            auto* actor = ref && ref->GetFormID() == r.spawnedId ? ref->As<RE::Actor>() : nullptr;
            // The leveled spell cache needs the high process (RE/A/Actor.h:587 GetHighProcess; 2026-10-10: before 3D, HasSpell was false)
            if (actor && actor->Is3DLoaded() && actor->GetHighProcess()) {
                SelfTest::Check(s, r, actor, waited);
                Cleanup(s, r);
                continue;
            }
            if (NpcSpells::SelfTestWaitOver(waited, s.config.maxWaitMs)) {
                SelfTest::Fail(s, r, std::format("no 3D or high process after {} ticks of {} ms (actor={} 3D={} high={})", waited, NpcSpells::kSelfTestPollMs,
                    actor ? "yes" : "no", actor && actor->Is3DLoaded() ? "yes" : "no", actor && actor->GetHighProcess() ? "yes" : "no"));
                Cleanup(s, r);
                continue;
            }
            open = true;
        }
        if (!open) {
            Finish(s);
        }
    }

    // Nothing leaves an SKSE task: an error ends the test with every sample cleaned up and failed (RESTART before the spawn)
    void StopOnError(State& s, std::string_view what) noexcept
    {
        try {
            SelfTest::Error(s, std::format("SELFTEST stopped by an error: {}", what));
            End(s, std::format("stopped by an error: {}", what), true);
        } catch (...) {
        }
        s.finished = true;
    }

    void RunTick(State& s) noexcept
    {
        if (s.finished) {
            return;
        }
        try {
            Tick(s);
        } catch (const std::exception& e) {
            StopOnError(s, e.what());
        } catch (...) {
            StopOnError(s, "unknown error");
        }
    }

    // A test that could not start: its one line in the log and in the report
    void ReportNotStarted(std::string_view why) noexcept
    {
        try {
            const auto line = std::format("SELFTEST DONE pass=0 fail=0 inconclusive=0 (not started: {})", why);
            logger::error("{}", line);
            AppendReport({ line });
        } catch (...) {
        }
    }

    void Start()
    {
        std::ifstream file(NpcSpellsGame::kSelfTestPath, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        auto state = std::make_shared<State>();
        std::string error;
        if (!file || !NpcSpells::ParseSelfTestConfig(text.str(), state->config, error)) {
            ReportNotStarted(std::format("{} - {}", NpcSpellsGame::kSelfTestPath, error.empty() ? "unreadable" : error));
            return;
        }
        if (!error.empty()) {
            SelfTest::Warn(*state, std::format("SELFTEST config: {}", error));
        }
        for (std::size_t i = 0; i < state->config.samples.size(); ++i) {
            auto& r = state->samples.emplace_back();
            r.text = state->config.sampleTexts[i];
            r.ref = state->config.samples[i];
        }
        if (auto* dh = RE::TESDataHandler::GetSingleton()) {
            state->ours.file = dh->LookupLoadedLightModByName(NpcSpells::kEslFileName);  // RE/T/TESDataHandler.h:78
        }
        SelfTest::Log(*state, std::format("SELFTEST start samples={} startDelayMs={} maxWaitMs={} pollMs={} esl={}", state->samples.size(), state->config.startDelayMs,
            state->config.maxWaitMs, NpcSpells::kSelfTestPollMs, state->ours.file ? "loaded" : "not loaded"));
        {
            std::lock_guard lock(g_currentLock);
            g_current = state;
        }
        // Runs until the test finishes: every wait is a finite number of ticks, so the ticks end it. A tick that never
        // runs (the game quit) leaves this thread asleep in its loop until the process ends; it writes no result.
        // Nothing escapes the thread (std::terminate): a throw (no memory) frees the queue slot and the loop goes on.
        std::thread([state] {
            while (!state->finished) {
                try {
                    std::this_thread::sleep_for(std::chrono::milliseconds(NpcSpells::kSelfTestPollMs));
                    if (state->queued.exchange(true)) {
                        continue;  // the last tick has not run yet
                    }
                    const auto* tasks = SKSE::GetTaskInterface();  // SKSE/API.h:58
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
}

void NpcSpellsGame::MaybeStartSelfTest()
{
    if (g_started) {  // SKSE sends the load messages on the main thread
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(kSelfTestPath, ec)) {
        return;
    }
    g_started = true;
    try {
        Start();
    } catch (const std::exception& e) {
        ReportNotStarted(e.what());
    } catch (...) {
        ReportNotStarted("unknown error");
    }
}

void NpcSpellsGame::OnSelfTestPreLoad()
{
    const auto s = Current();
    if (!s || s->finished) {
        return;
    }
    if (!s->spawned) {  // nothing stands in the world yet: the wait starts again in the save being loaded
        s->notReadyTicks = 0;
        s->readyTicks = 0;
        SelfTest::Log(*s, "SELFTEST another save is loading before the spawn: the wait starts again");
        return;
    }
    s->aborted = true;
    s->abortReason = "another save was loaded during the test";
    SelfTest::Warn(*s, "SELFTEST WARN another save is loading during the test: the next tick ends it (the spawned refs go with the old world)");
}

void NpcSpellsGame::OnSelfTestNewGame()
{
    const auto s = Current();
    if (!s || s->finished) {
        return;
    }
    s->aborted = true;
    s->abortReason = "a new game was started during the test";
    SelfTest::Warn(*s, "SELFTEST WARN a new game was started during the test: the next tick ends it, nothing is spawned");
}

void NpcSpellsGame::OnSelfTestSave()
{
    const auto s = Current();
    if (!s || s->finished || s->liveActors <= 0) {
        return;
    }
    s->savedWithActors = true;
    logger::warn("SELFTEST WARN save taken while sample actors exist (actors={})", s->liveActors.load());
}
