#include "game/ToxMagicka.h"

#include "game/ToxBottles.h"
#include "game/ToxCheck.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"
#include "game/ToxMagickaShared.h"
#include "game/ToxSelection.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

using namespace ToxGame::MagickaDetail;

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // A hand's remembered fire-and-forget cost is used for a cast event only this long after it was seen
    constexpr double kCostMatchSeconds = 2.0;
    // A second cast event for the same fire-and-forget spell inside this window, while the spell was
    // dual cast, is the same release reported again and is not charged. Outside a dual cast every event
    // is charged.
    constexpr double kSameCastWindowSeconds = 0.05;

    using Clock = std::chrono::steady_clock;

    // =========================================================================
    // State
    // =========================================================================

    std::mutex g_accumulatedMutex;
    float g_accumulated = 0.0f;

    std::atomic<bool> g_castThreadLogged{ false };
    std::atomic<bool> g_noTaskLogged{ false };
    std::thread::id g_mainThread;  // written once by InstallMagickaTracking, before the hook and sink exist

    // Main thread only (the cast task)
    RE::FormID g_lastCastSpell = 0;
    double g_lastCastAt = -1.0;
    double g_lastReleaseAt = -1.0;
    // Steady-state lines: info the first time in the DLL's life, debug after
    bool g_chargeLogged = false;
    bool g_unmatchedLogged = false;
    bool g_dualRepeatLogged = false;
    bool g_singleRepeatLogged = false;

    spdlog::level::level_enum InfoOnce(bool& a_logged)
    {
        if (a_logged) return spdlog::level::debug;
        a_logged = true;
        return spdlog::level::info;
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
    // Fire-and-forget (cast event task, main thread)
    // =========================================================================

    bool FreshFor(const HandSample& a_hand, RE::FormID a_spell, double a_now)
    {
        return a_hand.spell == a_spell && a_hand.seenAt >= 0.0 && a_now - a_hand.seenAt <= kCostMatchSeconds;
    }

    // Both hands' last fire-and-forget readings, once per stretch of releases (a pause over kCheckGapSeconds
    // starts a new stretch), so the self test can tell which value tracks the charge
    void LogReleaseHands(const RE::MagicItem& a_spell, RE::FormID a_spellId, const std::array<HandSample, kHandCount>& a_hands, double a_now)
    {
        const bool newStretch = g_lastReleaseAt < 0.0 || a_now - g_lastReleaseAt > ToxGame::kCheckGapSeconds;
        g_lastReleaseAt = a_now;
        if (!newStretch) return;
        for (std::size_t hand = 0; hand < kHandCount; ++hand) {
            const auto& s = a_hands[hand];
            logger::info("Toxicology: fire-and-forget release of {} {:08X}, {} hand reading: spell {:08X} ({:.2f} s old), "
                "currentSpellCost {:.2f}, GetCurrentSpellCost {:.2f}, costCharged {:.2f}, dual {}", a_spell.GetName(), a_spellId,
                HandName(hand), s.spell, s.seenAt >= 0.0 ? a_now - s.seenAt : -1.0, s.currentSpellCost, s.spellCost, s.costCharged,
                s.dualCasting);
        }
    }

    void OnPlayerCast(RE::FormID a_spellId)
    {
        ToxGame::ReloadPoisonSpellsIfChanged();  // contract (ToxData.h)
        ToxGame::PublishMagickaTracking(ToxGame::GetSettings().enabled && ToxGame::PlayerHasAbility());
        if (!ToxGame::IsPoisonCostSpell(a_spellId)) return;
        auto* form = RE::TESForm::LookupByID(a_spellId);
        auto* spell = form ? form->As<RE::MagicItem>() : nullptr;
        // Concentration also raises the event when it starts; the hook counts it frame by frame
        if (!spell || spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget) return;  // RE/M/MagicItem.h:83
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        const double now = NowSeconds();
        const auto hands = FireHands();
        // The freshest hand holding this spell; a dual flag on either hand marks a dual cast
        std::size_t matched = kNoHand;
        bool dual = player->IsDualCasting();  // RE/A/Actor.h:637
        for (std::size_t hand = 0; hand < kHandCount; ++hand) {
            if (!FreshFor(hands[hand], a_spellId, now)) continue;
            dual = dual || hands[hand].dualCasting;
            if (matched == kNoHand || hands[hand].seenAt > hands[matched].seenAt) matched = hand;
        }

        if (a_spellId == g_lastCastSpell && now - g_lastCastAt < kSameCastWindowSeconds) {
            if (dual) {
                spdlog::log(InfoOnce(g_dualRepeatLogged), "Toxicology: second cast event for {:08X} inside {} s of a dual cast - "
                    "one release, not charged (repeats at debug level)", a_spellId, kSameCastWindowSeconds);
                return;
            }
            spdlog::log(InfoOnce(g_singleRepeatLogged), "Toxicology: second cast event for {:08X} inside {} s, not a dual cast - "
                "charged as its own release (repeats at debug level)", a_spellId, kSameCastWindowSeconds);
        }
        g_lastCastSpell = a_spellId;
        g_lastCastAt = now;
        LogReleaseHands(*spell, a_spellId, hands, now);

        // Only a hand reading is charged: the instant caster (scripts, our own poison casts, the self
        // test) raises the same event with no hand reading, and what the engine took for it is unknown
        if (matched == kNoHand) {
            spdlog::log(InfoOnce(g_unmatchedLogged), "Toxicology: fire-and-forget poison spell {} {:08X} cast with no hand "
                "reading of it in the last {} s - not charged (repeats at debug level)", spell->GetName(), a_spellId, kCostMatchSeconds);
            return;
        }
        const auto& s = hands[matched];
        spdlog::log(InfoOnce(g_chargeLogged), "Toxicology: fire-and-forget poison spell {} {:08X}: {:.2f} Magicka ({} hand "
            "currentSpellCost; GetCurrentSpellCost {:.2f}, costCharged {:.2f}, dual {}; repeats at debug level)", spell->GetName(),
            a_spellId, s.currentSpellCost, HandName(matched), s.spellCost, s.costCharged, dual);
        CountCast(s.currentSpellCost, matched);
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
}

// =============================================================================
// ToxGame::MagickaDetail (ToxMagickaShared.h)
// =============================================================================

double ToxGame::MagickaDetail::NowSeconds()
{
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void ToxGame::MagickaDetail::LogThreadOnce(std::atomic<bool>& a_flag, const char* a_what)
{
    if (a_flag.exchange(true)) return;
    const bool onMain = std::this_thread::get_id() == g_mainThread;
    logger::info("Toxicology: {} on the {} thread", a_what, onMain ? "main" : "a non-main");
}

bool ToxGame::MagickaDetail::QueueTask(std::function<void()> a_task)
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

void ToxGame::MagickaDetail::CountCast(float a_amount, std::size_t a_hand)
{
    if (CountSpent(a_amount)) ToxGame::NoteCountedForCheck(a_amount, a_hand);
}

const char* ToxGame::MagickaDetail::HandName(std::size_t a_hand)
{
    return a_hand == kLeftHand ? "left" : (a_hand == kRightHand ? "right" : "no");
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
    ResetHookState();
    ToxGame::ResetCheck();
    g_lastCastSpell = 0;
    g_lastCastAt = -1.0;
    g_lastReleaseAt = -1.0;
}

void ToxGame::PublishMagickaTracking(bool a_abilityAndEnabled)
{
    SetTracking(a_abilityAndEnabled && !PoisonCostSnapshot()->empty());
}

void ToxGame::InstallMagickaTracking()
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    g_mainThread = std::this_thread::get_id();

    InstallCasterHook();

    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::error("Toxicology: no ScriptEventSourceHolder at kDataLoaded - fire-and-forget poison spells cost no bottles");
        return;
    }
    holder->AddEventSink<RE::TESSpellCastEvent>(CastSink::GetSingleton());  // RE/S/ScriptEventSourceHolder.h:107,147
    logger::info("Toxicology: cast sink registered (TESSpellCastEvent)");
}
