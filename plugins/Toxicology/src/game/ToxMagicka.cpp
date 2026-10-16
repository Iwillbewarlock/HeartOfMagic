#include "game/ToxMagicka.h"

#include "game/ToxBottles.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"
#include "game/ToxSelection.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // ActorMagicCaster::Update(float) on SE/AE (RE/A/ActorMagicCaster.h:70-81; this DLL is built
    // without VR, plugins/cmake/commonlibsse.cmake:40, so EXCLUSIVE_SKYRIM_FLAT and 0x1D)
    constexpr std::size_t kUpdateVfuncIndex = 0x1D;

    // The caster state a concentration spell is counted in. RE/M/MagicCaster.h:30-42 names none
    // "concentrating"; the states seen with a poison concentration spell are logged once per value,
    // and kCasting (6) stands until that log says otherwise.
    constexpr auto kConcentratingState = RE::MagicCaster::State::kCasting;

    constexpr std::size_t kHandCount = 2;  // MagicSystem::CastingSource kLeftHand 0, kRightHand 1 (RE/M/MagicSystem.h:26-30)

    // A hand's remembered fire-and-forget cost is used for a cast event only this long after it was seen
    constexpr double kCostMatchSeconds = 2.0;
    // A second cast event for the same fire-and-forget spell inside this window is the same release
    // (a dual cast may report once per hand) and is not charged again
    constexpr double kSameCastWindowSeconds = 0.05;

    // Debug check (spec 5.6): every kCheckWindowSeconds of poison casting, Magicka counted vs the
    // player's Magicka drop. A pause longer than kCheckGapSeconds starts a new window.
    constexpr double kCheckWindowSeconds = 5.0;
    constexpr double kCheckGapSeconds = 2.0;

    using Clock = std::chrono::steady_clock;

    double NowSeconds()
    {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    // =========================================================================
    // State shared with the hook and the cast sink (g_sharedMutex)
    // =========================================================================

    struct HandCost
    {
        RE::FormID spell = 0;
        float cost = 0.0f;         // MagicCaster::currentSpellCost (RE/M/MagicCaster.h:108)
        float costCharged = 0.0f;  // ActorMagicCaster::costCharged (RE/A/ActorMagicCaster.h), logged beside it
        bool dualCasting = false;  // ActorMagicCaster::Flags::kDualCasting
        double seenAt = -1.0;
    };

    struct Shared
    {
        std::unordered_map<RE::FormID, float> concentration;      // spell -> Magicka not yet handed to a task
        std::unordered_map<RE::FormID, std::uint32_t> newStates;  // spell -> state bits not yet handed to a task
        std::unordered_map<RE::FormID, std::uint32_t> statesSeen; // spell -> state bits already handed (whole session)
        std::array<HandCost, kHandCount> hands{};
        bool drainQueued = false;
    };

    std::mutex g_sharedMutex;
    Shared g_shared;

    std::mutex g_accumulatedMutex;
    float g_accumulated = 0.0f;

    std::atomic<bool> g_hookThreadLogged{ false };
    std::atomic<bool> g_hookFailureLogged{ false };
    std::atomic<bool> g_castThreadLogged{ false };
    std::atomic<bool> g_noTaskLogged{ false };
    std::thread::id g_mainThread;  // written once by InstallMagickaTracking, before the hook and sink exist

    // =========================================================================
    // State of the main thread
    // =========================================================================

    struct CheckWindow
    {
        bool open = false;
        double openedAt = 0.0;
        double lastAt = 0.0;
        float startMagicka = 0.0f;
        float counted = 0.0f;
    };

    CheckWindow g_check;
    RE::FormID g_lastCastSpell = 0;
    double g_lastCastAt = -1.0;
    std::uint32_t g_poisonStatesLogged = 0;

    void LogThreadOnce(std::atomic<bool>& a_flag, const char* a_what)
    {
        if (a_flag.exchange(true)) return;
        const bool onMain = std::this_thread::get_id() == g_mainThread;
        logger::info("Toxicology: {} on the {} thread", a_what, onMain ? "main" : "a non-main");
    }

    bool QueueTask(std::function<void()> a_task)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            if (!g_noTaskLogged.exchange(true)) {
                logger::error("Toxicology: no SKSE task interface - Magicka spent on poison spells is not counted");
            }
            return false;
        }
        tasks->AddTask(std::move(a_task));  // SKSE/Interfaces.h:394; runs on the game's main thread
        return true;
    }

    float PlayerMagicka(RE::PlayerCharacter& a_player)
    {
        auto* owner = a_player.AsActorValueOwner();  // RE/A/Actor.h:778
        return owner ? owner->GetActorValue(RE::ActorValue::kMagicka) : 0.0f;  // RE/A/ActorValueOwner.h:16
    }

    // The body of AddSpentMagicka; true when the amount was counted
    bool CountSpent(float a_amount)
    {
        if (!std::isfinite(a_amount) || a_amount <= 0.0f) return false;
        if (!ToxGame::CurrentPoison()) return false;  // no ability, disabled or no favorited poison (spec 5.6)
        Toxicology::DoseResult result;
        {
            std::lock_guard lock(g_accumulatedMutex);
            result = Toxicology::ConsumeMagicka(g_accumulated, a_amount, ToxGame::GetSettings().magickaPerDose);
            g_accumulated = result.remainder;
        }
        if (result.doses > 0) ToxGame::UseDoses(result.doses);
        return true;
    }

    // =========================================================================
    // Debug check (spec 5.6)
    // =========================================================================

    // The first counted amount of a window was charged before the window reads the player's Magicka,
    // so it is left out of both sides
    void NoteForCheck(float a_amount)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        const double now = NowSeconds();
        const float magicka = PlayerMagicka(*player);
        if (!g_check.open || now - g_check.lastAt > kCheckGapSeconds) {
            g_check = { true, now, now, magicka, 0.0f };
            return;
        }
        g_check.counted += a_amount;
        g_check.lastAt = now;
        if (now - g_check.openedAt >= kCheckWindowSeconds) {
            logger::info("Toxicology: magicka counted {:.2f} / player magicka drop {:.2f} over {:.1f} s",
                g_check.counted, g_check.startMagicka - magicka, now - g_check.openedAt);
            g_check.open = false;
        }
    }

    void CountCast(float a_amount)
    {
        if (CountSpent(a_amount)) NoteForCheck(a_amount);
    }

    // =========================================================================
    // Concentration (tasks, main thread)
    // =========================================================================

    void LogPoisonStates(RE::FormID a_spell, std::uint32_t a_bits)
    {
        const auto fresh = a_bits & ~g_poisonStatesLogged;
        if (!fresh) return;
        g_poisonStatesLogged |= fresh;
        const auto* form = RE::TESForm::LookupByID(a_spell);
        for (std::uint32_t value = 0; value < 32; ++value) {
            if (fresh & (1u << value)) {
                logger::info("Toxicology: a hand caster holding poison concentration spell {} {:08X} is in state {} "
                    "(counted in state {})", form ? form->GetName() : "?", a_spell, value, std::to_underlying(kConcentratingState));
            }
        }
    }

    void DrainConcentration()
    {
        std::unordered_map<RE::FormID, float> amounts;
        std::unordered_map<RE::FormID, std::uint32_t> states;
        {
            std::lock_guard lock(g_sharedMutex);
            amounts.swap(g_shared.concentration);
            states.swap(g_shared.newStates);
            g_shared.drainQueued = false;
        }
        if (amounts.empty() && states.empty()) return;
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h): before every IsPoisonSpell
        for (const auto& [spell, bits] : states) {
            if (ToxGame::IsPoisonSpell(spell)) LogPoisonStates(spell, bits);
        }
        for (const auto& [spell, amount] : amounts) {
            if (ToxGame::IsPoisonSpell(spell)) CountCast(amount);
        }
    }

    void QueueDrain()
    {
        const bool queued = QueueTask([]() {
            try {
                DrainConcentration();
            } catch (const std::exception& e) {
                logger::error("Toxicology: concentration Magicka task failed: {}", e.what());
            }
        });
        if (!queued) {
            std::lock_guard lock(g_sharedMutex);
            g_shared.drainQueued = false;
            g_shared.concentration.clear();
            g_shared.newStates.clear();
        }
    }

    // =========================================================================
    // Fire-and-forget (cast event task, main thread)
    // =========================================================================

    // The freshest hand cost remembered for a_spell, if one was seen inside kCostMatchSeconds
    std::optional<HandCost> RememberedCost(RE::FormID a_spell, double a_now)
    {
        std::lock_guard lock(g_sharedMutex);
        std::optional<HandCost> best;
        for (const auto& hand : g_shared.hands) {
            if (hand.spell != a_spell || hand.seenAt < 0.0 || a_now - hand.seenAt > kCostMatchSeconds) continue;
            if (!best || hand.seenAt > best->seenAt) best = hand;
        }
        return best;
    }

    void OnPlayerCast(RE::FormID a_spellId)
    {
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h)
        if (!ToxGame::IsPoisonSpell(a_spellId)) return;
        auto* form = RE::TESForm::LookupByID(a_spellId);
        auto* spell = form ? form->As<RE::MagicItem>() : nullptr;
        // Concentration also raises the event when it starts; the hook counts it frame by frame
        if (!spell || spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget) return;  // RE/M/MagicItem.h:83
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        const double now = NowSeconds();
        if (a_spellId == g_lastCastSpell && now - g_lastCastAt < kSameCastWindowSeconds) {
            logger::debug("Toxicology: second cast event for {:08X} inside {} s - same release, not charged", a_spellId, kSameCastWindowSeconds);
            return;
        }
        g_lastCastSpell = a_spellId;
        g_lastCastAt = now;

        float cost = 0.0f;
        if (const auto hand = RememberedCost(a_spellId, now)) {
            cost = hand->cost;
            logger::info("Toxicology: fire-and-forget poison spell {} {:08X}: {:.2f} Magicka (caster cost; costCharged {:.2f}, dual {})",
                spell->GetName(), a_spellId, cost, hand->costCharged, hand->dualCasting);
        } else {
            cost = spell->CalculateMagickaCost(player);  // RE/M/MagicItem.h:112
            const bool dual = player->IsDualCasting();   // RE/A/Actor.h:637
            if (dual) cost = RE::MagicFormulas::CalcDualCastCost(cost);  // RE/M/MagicFormulas.h:7
            logger::info("Toxicology: fire-and-forget poison spell {} {:08X}: {:.2f} Magicka (no caster cost seen; calculated, dual {})",
                spell->GetName(), a_spellId, cost, dual);
        }
        CountCast(cost);
    }

    // =========================================================================
    // Cast event sink
    // =========================================================================

    class CastSink final : public RE::BSTEventSink<RE::TESSpellCastEvent>
    {
    public:
        static CastSink* GetSingleton()
        {
            static CastSink singleton;
            return &singleton;
        }

        CastSink(const CastSink&) = delete;
        CastSink& operator=(const CastSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/T/TESSpellCastEvent.h:13-14. Any thread (plugins/spelllearning/src/
        // SpellCastHandler.cpp:63-66): only the spell id and the generation go into the task.
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event,
            RE::BSTEventSource<RE::TESSpellCastEvent>*) override
        {
            if (!a_event || !a_event->object || !a_event->object->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
            LogThreadOnce(g_castThreadLogged, "player spell cast events arrive");
            const RE::FormID spellId = a_event->spell;
            const auto generation = ToxGame::SessionGeneration();
            QueueTask([spellId, generation]() {
                if (generation != ToxGame::SessionGeneration()) return;  // cast before a load or new game
                try {
                    OnPlayerCast(spellId);
                } catch (const std::exception& e) {
                    logger::error("Toxicology: cast task for spell [{:08X}] failed: {}", spellId, e.what());
                }
            });
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        CastSink() = default;
        ~CastSink() override = default;
    };

    // =========================================================================
    // ActorMagicCaster::Update hook
    // =========================================================================

    std::uint32_t StateBit(RE::MagicCaster::State a_state)
    {
        const auto value = static_cast<std::uint32_t>(std::to_underlying(a_state));
        return value < 32 ? (1u << value) : 0u;
    }

    // Before the original Update, so the frame a cast is released in is still seen as it was
    void Observe(RE::ActorMagicCaster& a_caster, float a_delta)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || a_caster.actor != player) return;  // RE/A/ActorMagicCaster.h:93
        const auto source = a_caster.castingSource;       // RE/A/ActorMagicCaster.h:100; kInstant (our own poison casts) is left out
        if (source != RE::MagicSystem::CastingSource::kLeftHand && source != RE::MagicSystem::CastingSource::kRightHand) return;
        auto* spell = a_caster.currentSpell;  // RE/M/MagicCaster.h:105
        if (!spell) return;
        LogThreadOnce(g_hookThreadLogged, "the player's hand casters update");

        const RE::FormID spellId = spell->GetFormID();
        const auto type = spell->GetCastingType();
        const auto state = a_caster.state.get();  // REX/REX/EnumSet.h:55
        if (type == RE::MagicSystem::CastingType::kConcentration) {
            float spent = 0.0f;
            if (state == kConcentratingState && std::isfinite(a_delta) && a_delta > 0.0f) {
                spent = a_caster.GetCurrentSpellCost() * a_delta;  // RE/M/MagicCaster.h:94
            }
            const auto bit = StateBit(state);
            bool queue = false;
            {
                std::lock_guard lock(g_sharedMutex);
                bool handed = false;
                auto& seen = g_shared.statesSeen[spellId];
                if (bit && !(seen & bit)) {
                    seen |= bit;
                    g_shared.newStates[spellId] |= bit;
                    handed = true;
                }
                if (std::isfinite(spent) && spent > 0.0f) {
                    g_shared.concentration[spellId] += spent;
                    handed = true;
                }
                if (handed && !g_shared.drainQueued) {
                    g_shared.drainQueued = true;
                    queue = true;
                }
            }
            if (queue) QueueDrain();
        } else if (type == RE::MagicSystem::CastingType::kFireAndForget) {
            const float cost = a_caster.currentSpellCost;  // RE/M/MagicCaster.h:108
            if (!std::isfinite(cost) || cost <= 0.0f) return;
            const auto hand = static_cast<std::size_t>(std::to_underlying(source));
            const bool dual = a_caster.flags.any(RE::ActorMagicCaster::Flags::kDualCasting);  // REX/REX/EnumSet.h:94
            std::lock_guard lock(g_sharedMutex);
            g_shared.hands[hand] = { spellId, cost, a_caster.costCharged, dual, NowSeconds() };
        }
    }

    struct UpdateHook
    {
        static void thunk(RE::ActorMagicCaster* a_this, float a_delta)
        {
            // Entered from the engine's vtable: nothing may throw out of here
            try {
                if (a_this) Observe(*a_this, a_delta);
            } catch (...) {
                if (!g_hookFailureLogged.exchange(true)) {
                    logger::error("Toxicology: caster update hook threw; further failures are not logged");
                }
            }
            func(a_this, a_delta);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };
}

// =============================================================================
// Public
// =============================================================================

void ToxGame::AddSpentMagicka(float a_amount)
{
    CountSpent(a_amount);
}

float ToxGame::Accumulated()
{
    std::lock_guard lock(g_accumulatedMutex);
    return g_accumulated;
}

void ToxGame::SetAccumulated(float a_value)
{
    std::lock_guard lock(g_accumulatedMutex);
    g_accumulated = a_value;
}

void ToxGame::ResetMagicka()
{
    {
        std::lock_guard lock(g_accumulatedMutex);
        g_accumulated = 0.0f;
    }
    {
        std::lock_guard lock(g_sharedMutex);
        g_shared.concentration.clear();
        g_shared.newStates.clear();
        g_shared.hands = {};
        g_shared.drainQueued = false;  // a task still queued drains whatever is pending when it runs
    }
    g_check = {};
    g_lastCastSpell = 0;
    g_lastCastAt = -1.0;
}

void ToxGame::InstallMagickaTracking()
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    g_mainThread = std::this_thread::get_id();

    // plugins/spelllearning/src/SpellCastImmediateHook.cpp:56-57; [0] is the MagicCaster base's vtable
    REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ActorMagicCaster[0] };
    UpdateHook::func = vtbl.write_vfunc(kUpdateVfuncIndex, UpdateHook::thunk);
    logger::info("Toxicology: hooked ActorMagicCaster::Update (Magicka spent on poison spells)");

    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::error("Toxicology: no ScriptEventSourceHolder at kDataLoaded - fire-and-forget poison spells cost no bottles");
        return;
    }
    holder->AddEventSink<RE::TESSpellCastEvent>(CastSink::GetSingleton());  // RE/S/ScriptEventSourceHolder.h:107,147
    logger::info("Toxicology: cast sink registered (TESSpellCastEvent)");
}
