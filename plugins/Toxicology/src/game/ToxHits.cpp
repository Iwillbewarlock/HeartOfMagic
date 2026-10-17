#include "game/ToxHits.h"

#include "game/ToxApply.h"
#include "game/ToxData.h"
#include "game/ToxSelection.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace
{
    // =========================================================================
    // State
    // =========================================================================

    std::atomic<std::uint32_t> g_generation{ 0 };   // any thread (the sink reads it)
    std::atomic<bool> g_threadLogged{ false };      // any thread
    std::atomic<bool> g_noTaskLogged{ false };      // any thread
    std::thread::id g_mainThread;                   // written once by RegisterHitSink, before the sink exists

    // Spec 5.3: one hit raises several TESHitEvents. Repeats of the same (target, spell) inside
    // this window are dropped whatever the first one did.
    constexpr double kSameHitWindowSeconds = 0.05;
    // The window map is swept of expired entries once it holds more than this many
    constexpr std::size_t kSameHitSweepSize = 64;

    using Clock = std::chrono::steady_clock;

    // (target FormID << 32 | spell FormID) -> last kept hit, steady_clock seconds (main thread only)
    std::unordered_map<std::uint64_t, double> g_lastHits;

    // Self test trace (ToxHits.h). The sink counts from any thread, so the map has a lock; the flag
    // is the only thing the sink reads while tracing is off.
    std::atomic<bool> g_tracing{ false };
    std::mutex g_traceMutex;
    std::unordered_map<RE::FormID, ToxGame::HitTrace> g_traces;

    template <class F>
    void Trace(RE::FormID a_spell, F&& a_update)
    {
        if (!g_tracing.load()) return;
        std::lock_guard lock(g_traceMutex);
        a_update(g_traces[a_spell]);
    }

    void TraceStage(RE::FormID a_spell, ToxGame::HitStage a_stage)
    {
        Trace(a_spell, [a_stage](ToxGame::HitTrace& a_trace) { a_trace.last = a_stage; });
    }

    // True for a repeat of the same hit; otherwise stamps (target, spell) and returns false
    bool IsSameHit(RE::FormID a_target, RE::FormID a_spell)
    {
        const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        if (g_lastHits.size() > kSameHitSweepSize) {
            std::erase_if(g_lastHits, [now](const auto& a_entry) { return now - a_entry.second >= kSameHitWindowSeconds; });
        }
        const auto key = (static_cast<std::uint64_t>(a_target) << 32) | a_spell;
        const auto it = g_lastHits.find(key);
        if (it != g_lastHits.end() && now - it->second < kSameHitWindowSeconds) return true;
        g_lastHits[key] = now;
        return false;
    }

    // =========================================================================
    // Task
    // =========================================================================

    void QueueHit(RE::ActorHandle a_target, RE::FormID a_spell, std::uint32_t a_generation)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            if (!g_noTaskLogged.exchange(true)) {
                logger::error("Toxicology: no SKSE task interface - poison spell hits are dropped");
            }
            return;
        }
        // SKSE/Interfaces.h:394; runs on the game's main thread
        tasks->AddTask([a_target, a_spell, a_generation]() {
            if (a_generation != ToxGame::SessionGeneration()) return;  // queued before a load or new game
            try {
                ToxGame::OnPoisonSpellHit(a_target, a_spell);
            } catch (const std::exception& e) {
                logger::error("Toxicology: hit task for spell [{:08X}] failed: {}", a_spell, e.what());
            }
        });
    }

    // =========================================================================
    // Hit sink (spec 5.3)
    // =========================================================================

    class HitSink final : public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        static HitSink* GetSingleton()
        {
            static HitSink singleton;
            return &singleton;
        }

        HitSink(const HitSink&) = delete;
        HitSink& operator=(const HitSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/T/TESHitEvent.h:26-30. Our own poison cast raises hits whose
        // source is the AlchemyItem: the Spell type check drops them (no re-entry).
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event,
            RE::BSTEventSource<RE::TESHitEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;
            Trace(a_event->source, [](ToxGame::HitTrace& a_trace) { ++a_trace.rawEvents; });
            if (!a_event->cause || !a_event->target) return RE::BSEventNotifyControl::kContinue;
            if (!a_event->cause->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;  // TESForm.h:352
            const auto* source = RE::TESForm::LookupByID(a_event->source);
            if (!source || !source->Is(RE::FormType::Spell)) return RE::BSEventNotifyControl::kContinue;
            auto* target = a_event->target->As<RE::Actor>();
            if (!target || target->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
            LogThreadOnce();
            Trace(a_event->source, [](ToxGame::HitTrace& a_trace) { ++a_trace.sinkHits; });
            QueueHit(target->GetHandle(), a_event->source, ToxGame::SessionGeneration());  // Actor.h:584
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        HitSink() = default;
        ~HitSink() override = default;

        static void LogThreadOnce()
        {
            if (g_threadLogged.exchange(true)) return;
            const bool onMain = std::this_thread::get_id() == g_mainThread;
            logger::info("Toxicology: spell hit events arrive on the {} thread", onMain ? "main" : "a non-main");
        }
    };
}

// =============================================================================
// Public
// =============================================================================

std::uint32_t ToxGame::SessionGeneration()
{
    return g_generation.load();
}

void ToxGame::BumpGeneration()
{
    g_generation.fetch_add(1);
}

void ToxGame::SetHitTracing(bool a_on)
{
    std::lock_guard lock(g_traceMutex);
    g_traces.clear();
    g_tracing.store(a_on);
}

ToxGame::HitTrace ToxGame::HitTraceFor(RE::FormID a_spell)
{
    std::lock_guard lock(g_traceMutex);
    const auto it = g_traces.find(a_spell);
    return it != g_traces.end() ? it->second : HitTrace{};
}

void ToxGame::OnPoisonSpellHit(RE::ActorHandle a_target, RE::FormID a_spell)
{
    Trace(a_spell, [](HitTrace& a_trace) { ++a_trace.tasks; });
    if (!GetSettings().enabled || !PlayerHasAbility()) return TraceStage(a_spell, HitStage::kOff);
    ReloadPoisonSpellsIfChanged();  // contract (ToxData.h): before every IsPoisonSpell
    if (!IsPoisonSpell(a_spell)) return TraceStage(a_spell, HitStage::kNotPoisonSpell);
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return TraceStage(a_spell, HitStage::kTargetGone);
    const auto targetRef = a_target.get();  // RE/B/BSPointerHandle.h:155; null once the actor is gone
    auto* target = targetRef.get();
    if (!target || target->IsPlayerRef() || target->IsDead()) return TraceStage(a_spell, HitStage::kTargetGone);  // Actor.h:363
    if (IsSameHit(target->GetFormID(), a_spell)) return TraceStage(a_spell, HitStage::kSameHit);
    auto* poison = CurrentPoison();  // null with no favorited poison (ToxSelection.h)
    if (!poison) return TraceStage(a_spell, HitStage::kNoPoison);
    const auto outcome = ApplyOnce(target, poison, player);
    Trace(a_spell, [outcome](HitTrace& a_trace) {
        ++a_trace.applied;
        a_trace.last = HitStage::kApplied;
        a_trace.outcome = outcome;
        if (const auto kind = static_cast<std::size_t>(outcome); kind < a_trace.outcomes.size()) ++a_trace.outcomes[kind];
    });
}

void ToxGame::RegisterHitSink()
{
    g_mainThread = std::this_thread::get_id();
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::error("Toxicology: no ScriptEventSourceHolder at kDataLoaded - poison spell hits are not seen");
        return;
    }
    holder->AddEventSink<RE::TESHitEvent>(HitSink::GetSingleton());  // RE/S/ScriptEventSourceHolder.h:83,147
    logger::info("Toxicology: hit sink registered (TESHitEvent)");
}
