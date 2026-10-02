#include "Common.h"
#include "CounterInput.h"
#include "CombatConfig.h"
#include "CounterKey.h"
#include "WardSupport.h"

namespace SLCombat
{
    namespace
    {
        float SecondsSince(std::int64_t a_ticks)
        {
            using namespace std::chrono;
            auto now = steady_clock::now().time_since_epoch().count();
            return duration<float>(steady_clock::duration(now - a_ticks)).count();
        }
    }

    bool IsWardSpell(const RE::SpellItem* a_spell)
    {
        if (!a_spell) {
            return false;
        }
        for (const auto* effect : a_spell->effects) {
            if (effect && effect->baseEffect &&
                effect->baseEffect->data.primaryAV == RE::ActorValue::kWardPower) {
                return true;
            }
        }
        return false;
    }

    CounterInput* CounterInput::GetSingleton()
    {
        static CounterInput instance;
        return &instance;
    }

    void CounterInput::Register()
    {
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink<RE::InputEvent*>(GetSingleton());
            logger::info("SL_Combat: input sink registered");
        } else {
            logger::error("SL_Combat: no input device manager - counter input disabled");
        }
    }

    RE::BSEventNotifyControl CounterInput::ProcessEvent(RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!a_event || !CombatConfig::Get().enableCounter) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->GameIsPaused()) {
            return RE::BSEventNotifyControl::kContinue;  // presses in menus are not counters
        }

        for (auto* event = *a_event; event; event = event->next) {
            auto* button = event->AsButtonEvent();
            if (!button || !CounterKey::Matches(button)) {
                continue;
            }
            if (button->IsDown()) {
                m_held.store(true);
            } else if (button->IsUp()) {
                m_held.store(false);
                WardSupport::ReleaseHold();
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    PressCheck CounterInput::Check() const
    {
        const auto& config = CombatConfig::Get();
        PressCheck check;
        auto ticks = m_pressedAt.load();
        if (ticks == 0) {
            return check;
        }
        check.secondsAgo = SecondsSince(ticks);
        check.ward = m_ward.load();
        float window = config.counterWindow + (check.ward ? config.wardBonusWindow : 0.0f);
        check.inWindow = check.secondsAgo <= window;
        return check;
    }

    void CounterInput::RecordPress(bool a_ward)
    {
        m_ward.store(a_ward);
        m_pressedAt.store(Clock::now().time_since_epoch().count());
    }

    void CounterInput::Consume()
    {
        m_pressedAt.store(0);
    }
}
