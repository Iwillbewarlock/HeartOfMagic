#include "game/ToxToggle.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"
#include "game/ToxSelection.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <optional>
#include <thread>
#include <utility>

namespace
{
    // =========================================================================
    // State
    // =========================================================================

    std::atomic<RE::FormID> g_powerId{ 0 };        // any thread (the sinks read it); 0 = no power
    std::atomic<RE::FormID> g_powerEffectId{ 0 };  // any thread; 0 = no power effect
    std::atomic<bool> g_castThreadLogged{ false };
    std::atomic<bool> g_applyThreadLogged{ false };
    std::atomic<bool> g_noTaskLogged{ false };
    std::thread::id g_mainThread;  // written once by RegisterPowerSinks, before the sinks exist

    // PowerTrace (ToxToggle.h)
    std::atomic<std::uint32_t> g_castEvents{ 0 };
    std::atomic<std::uint32_t> g_applyEvents{ 0 };
    std::atomic<std::uint32_t> g_toggles{ 0 };
    std::atomic<std::uint32_t> g_repeats{ 0 };
    std::atomic<std::uint32_t> g_failures{ 0 };

    using Clock = std::chrono::steady_clock;
    std::optional<double> g_lastToggleAt;  // main thread: steady seconds of the last accepted toggle

    double NowSeconds()
    {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    void LogThreadOnce(std::atomic<bool>& a_flag, const char* a_what)
    {
        if (a_flag.exchange(true)) return;
        const bool onMain = std::this_thread::get_id() == g_mainThread;
        logger::info("Toxicology: {} on the {} thread", a_what, onMain ? "main" : "a non-main");
    }

    // =========================================================================
    // Task
    // =========================================================================

    // a_body runs on the main thread unless a load or a new game came first
    template <class F>
    void QueuePowerTask(const char* a_what, F&& a_body)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            if (!g_noTaskLogged.exchange(true)) {
                logger::error("Toxicology: no SKSE task interface - the power does not turn Toxicology on or off");
            }
            return;
        }
        const auto generation = ToxGame::SessionGeneration();
        // SKSE/Interfaces.h:394; runs on the game's main thread
        tasks->AddTask([a_what, generation, body = std::forward<F>(a_body)]() {
            if (generation != ToxGame::SessionGeneration()) {
                logger::info("Toxicology: power: {} task queued before a load or new game - dropped", a_what);
                return;
            }
            try {
                body();
            } catch (const std::exception& e) {
                logger::error("Toxicology: power: {} task failed: {}", a_what, e.what());
            }
        });
    }

    void QueueToggle(ToxGame::PowerPath a_path)
    {
        QueuePowerTask(ToxGame::PowerPathName(a_path), [a_path] { ToxGame::TogglePower(a_path); });
    }

    // =========================================================================
    // Sinks
    // =========================================================================

    class PowerCastSink final : public RE::BSTEventSink<RE::TESSpellCastEvent>
    {
    public:
        static PowerCastSink* GetSingleton()
        {
            static PowerCastSink singleton;
            return &singleton;
        }

        PowerCastSink(const PowerCastSink&) = delete;
        PowerCastSink& operator=(const PowerCastSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/T/TESSpellCastEvent.h:13-14 (object, spell)
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event,
            RE::BSTEventSource<RE::TESSpellCastEvent>*) override
        {
            const RE::FormID power = g_powerId.load();
            if (!a_event || power == 0 || a_event->spell != power) return RE::BSEventNotifyControl::kContinue;
            if (!a_event->object || !a_event->object->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;  // TESForm.h:352
            LogThreadOnce(g_castThreadLogged, "the power's spell cast events arrive");
            ++g_castEvents;
            QueueToggle(ToxGame::PowerPath::kSpellCast);
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        PowerCastSink() = default;
        ~PowerCastSink() override = default;
    };

    class PowerEffectSink final : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
    {
    public:
        static PowerEffectSink* GetSingleton()
        {
            static PowerEffectSink singleton;
            return &singleton;
        }

        PowerEffectSink(const PowerEffectSink&) = delete;
        PowerEffectSink& operator=(const PowerEffectSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/T/TESMagicEffectApplyEvent.h:13-15 (target, caster, magicEffect). Raised for every
        // effect applied to any actor: the FormID comparison comes first.
        RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event,
            RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
        {
            const RE::FormID effect = g_powerEffectId.load();
            if (!a_event || effect == 0 || a_event->magicEffect != effect) return RE::BSEventNotifyControl::kContinue;
            if (!a_event->caster || !a_event->caster->IsPlayerRef() || !a_event->target || !a_event->target->IsPlayerRef()) {
                return RE::BSEventNotifyControl::kContinue;
            }
            LogThreadOnce(g_applyThreadLogged, "the power's effect apply events arrive");
            ++g_applyEvents;
            QueueToggle(ToxGame::PowerPath::kEffectApply);
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        PowerEffectSink() = default;
        ~PowerEffectSink() override = default;
    };

    // =========================================================================
    // Toggle
    // =========================================================================

    // The message's name as the HUD notification, in the installed ESL's language. RE::SendHUDMessage::ShowHUDMessage
    // (RE/S/SendHUDMessage.h:9, src/RE/S/SendHUDMessage.cpp:14-19) is the function Debug.Notification uses; CommonLib
    // v10.0.0 has no RE::DebugNotification.
    void Notify(const RE::BGSMessage* a_message, const char* a_which)
    {
        if (!a_message) {
            logger::warn("Toxicology: power: the {} message (MESG) is not in the ESL - no notification", a_which);
            return;
        }
        const char* text = a_message->GetFullName();  // RE/T/TESFullName.h:23 (FULL)
        if (!text || !*text) {
            logger::warn("Toxicology: power: the {} message {:08X} has no text (FULL) - no notification", a_which,
                a_message->GetFormID());
            return;
        }
        RE::SendHUDMessage::ShowHUDMessage(text);
    }
}

// =============================================================================
// Public
// =============================================================================

const char* ToxGame::PowerPathName(PowerPath a_path)
{
    switch (a_path) {
    case PowerPath::kSpellCast:
        return "spell cast event";
    case PowerPath::kEffectApply:
        return "effect apply event";
    }
    return "?";
}

ToxGame::PowerTrace ToxGame::PowerTraceNow()
{
    return { g_castEvents.load(), g_applyEvents.load(), g_toggles.load(), g_repeats.load(), g_failures.load() };
}

void ToxGame::RegisterPowerSinks()
{
    g_mainThread = std::this_thread::get_id();
    auto* power = Power();
    auto* effect = PowerEffect();
    if (!power || !effect) {
        logger::error("Toxicology: {} not in the ESL - Toxicology cannot be turned on or off",
            !power && !effect ? "the power (0x805) and its effect (0x804) are" : (!power ? "the power (0x805) is" : "the power's effect (0x804) is"));
        return;
    }
    g_powerId.store(power->GetFormID());
    g_powerEffectId.store(effect->GetFormID());
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::error("Toxicology: no ScriptEventSourceHolder at kDataLoaded - the power does not turn Toxicology on or off");
        return;
    }
    holder->AddEventSink<RE::TESSpellCastEvent>(PowerCastSink::GetSingleton());          // RE/S/ScriptEventSourceHolder.h:107,147
    holder->AddEventSink<RE::TESMagicEffectApplyEvent>(PowerEffectSink::GetSingleton());  // RE/S/ScriptEventSourceHolder.h:87,147
    logger::info("Toxicology: power sinks registered (TESSpellCastEvent for {:08X}, TESMagicEffectApplyEvent for {:08X})",
        power->GetFormID(), effect->GetFormID());
}

void ToxGame::ResetPowerToggle()
{
    g_lastToggleAt.reset();
}

bool ToxGame::TogglePower(PowerPath a_path)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ability = Ability();
    if (!player || !ability) {
        ++g_failures;
        logger::error("Toxicology: power used ({}): no {} - nothing changed", PowerPathName(a_path), player ? "ability" : "player");
        return false;
    }
    const double now = NowSeconds();
    if (Toxicology::IsRepeatToggle(g_lastToggleAt, now, kRepeatToggleSeconds)) {  // core/ToxCore.h, checked by tools/toxicology-test
        ++g_repeats;
        logger::info("Toxicology: power used ({}): {:.3f} s after the last toggle - the same use, dropped as a repeat",
            PowerPathName(a_path), now - *g_lastToggleAt);
        return false;
    }
    g_lastToggleAt = now;

    const bool before = player->HasSpell(ability);  // RE/A/Actor.h:620
    const auto action = Toxicology::DecideToggle(before);
    if (action == Toxicology::ToggleAction::kRemove) {
        player->RemoveSpell(ability);  // RE/A/Actor.h:678
    } else {
        player->AddSpell(ability);  // RE/A/Actor.h:518
    }
    const bool after = player->HasSpell(ability);
    if (after == before) {
        ++g_failures;
        logger::error("Toxicology: power used ({}): {} the ability {:08X} left it {} - Toxicology stays {}", PowerPathName(a_path),
            Toxicology::ToggleActionName(action), ability->GetFormID(), after ? "held" : "not held", after ? "on" : "off");
        FlushLog();
        return false;
    }
    ++g_toggles;
    const bool enabled = GetSettings().enabled;
    logger::info("Toxicology: power used ({}): ability {} - Toxicology {}{}", PowerPathName(a_path),
        after ? "added" : "removed", after ? "on" : "off",
        enabled ? "" : " (the setting \"enabled\" is false: the feature does nothing until it is turned on in the settings)");
    Notify(after ? MessageOn() : MessageOff(), after ? "on" : "off");
    // The new state at once: the caster hook's flag (through CurrentPoison) and the icon keyword
    OnSelectionMaybeChanged();
    FlushLog();
    return true;
}

void ToxGame::QueuePowerMigration()
{
    QueuePowerTask("load migration", [] {
        MigratePower();
        FlushLog();
    });
}

Toxicology::MigrationAction ToxGame::MigratePower()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* power = Power();
    if (!player || !power || !Ability()) {
        logger::info("Toxicology: power at load: no {} - nothing to do", !player ? "player" : (power ? "ability" : "power"));
        return Toxicology::MigrationAction::kNothing;
    }
    const bool hasAbility = PlayerHasAbility();
    const bool hasPower = PlayerHasPower();
    const auto action = Toxicology::DecideMigration(hasAbility, hasPower);  // core/ToxCore.h, checked by tools/toxicology-test
    if (action == Toxicology::MigrationAction::kNothing) {
        logger::info("Toxicology: power at load: {} - ability {}, power {}", Toxicology::MigrationActionName(action),
            hasAbility ? "held" : "not held", hasPower ? "known" : "not known");
        return action;
    }
    player->AddSpell(power);  // RE/A/Actor.h:518
    const bool granted = PlayerHasPower();
    spdlog::log(granted ? spdlog::level::info : spdlog::level::err, "Toxicology: power at load: {} - the save knows the ability "
        "but not the power (a 1.0 save): {} (Toxicology stays on)", Toxicology::MigrationActionName(action),
        granted ? "power added" : "AddSpell left the power unknown");
    return action;
}
