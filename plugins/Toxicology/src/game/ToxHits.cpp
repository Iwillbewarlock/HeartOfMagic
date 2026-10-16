#include "game/ToxHits.h"

#include "game/ToxApply.h"
#include "game/ToxData.h"
#include "game/ToxSelection.h"

#include <atomic>
#include <exception>
#include <thread>

namespace
{
    // =========================================================================
    // State
    // =========================================================================

    std::atomic<std::uint32_t> g_generation{ 0 };   // any thread (the sink reads it)
    std::atomic<bool> g_threadLogged{ false };      // any thread
    std::atomic<bool> g_noTaskLogged{ false };      // any thread
    std::thread::id g_mainThread;                   // written once by RegisterHitSink, before the sink exists

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
            if (!a_event || !a_event->cause || !a_event->target) return RE::BSEventNotifyControl::kContinue;
            if (!a_event->cause->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;  // TESForm.h:352
            const auto* source = RE::TESForm::LookupByID(a_event->source);
            if (!source || !source->Is(RE::FormType::Spell)) return RE::BSEventNotifyControl::kContinue;
            auto* target = a_event->target->As<RE::Actor>();
            if (!target || target->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
            LogThreadOnce();
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

void ToxGame::OnPoisonSpellHit(RE::ActorHandle a_target, RE::FormID a_spell)
{
    if (!GetSettings().enabled || !PlayerHasAbility()) return;
    ReloadPoisonSpellsIfChanged();  // contract (ToxData.h): before every IsPoisonSpell
    if (!IsPoisonSpell(a_spell)) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    const auto targetRef = a_target.get();  // RE/B/BSPointerHandle.h:155; null once the actor is gone
    auto* target = targetRef.get();
    if (!target || target->IsPlayerRef() || target->IsDead()) return;  // Actor.h:363
    auto* poison = CurrentPoison();  // null with no favorited poison (ToxSelection.h)
    if (!poison) return;
    ApplyOrRefresh(target, poison, player);
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
