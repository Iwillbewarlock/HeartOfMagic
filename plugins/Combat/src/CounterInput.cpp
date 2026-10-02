#include "Common.h"
#include "CounterInput.h"
#include "CombatConfig.h"
#include "WardSupport.h"

namespace SLCombat
{
    namespace
    {
        RE::SpellItem* SpellInHand(RE::PlayerCharacter* a_player, bool a_leftHand)
        {
            auto* form = a_player->GetEquippedObject(a_leftHand);
            return form ? form->As<RE::SpellItem>() : nullptr;
        }

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

    void CounterInput::Record(HandState& a_state, bool a_leftHand)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        auto* spell = SpellInHand(player, a_leftHand);
        if (!spell) {
            return;  // a weapon or empty hand: not a counter press
        }
        a_state.ward.store(IsWardSpell(spell));
        a_state.pressedAt.store(Clock::now().time_since_epoch().count());
    }

    RE::BSEventNotifyControl CounterInput::ProcessEvent(RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!a_event || !CombatConfig::Get().enableCounter) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->GameIsPaused()) {
            return RE::BSEventNotifyControl::kContinue;  // presses in menus are not casts
        }
        auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            auto* button = event->AsButtonEvent();
            if (!button) {
                continue;
            }
            const auto& name = button->QUserEvent();
            bool left = name == userEvents->leftAttack;
            if (!left && name != userEvents->rightAttack) {
                continue;
            }
            auto& state = left ? m_left : m_right;
            Hand hand = left ? Hand::kLeft : Hand::kRight;
            if (button->IsDown()) {
                state.held.store(true);
            } else if (button->IsUp()) {
                state.held.store(false);
                WardSupport::ReleaseHold(hand);
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    PressCheck CounterInput::Check() const
    {
        const auto& config = CombatConfig::Get();
        PressCheck best;

        // Returns whether this hand's press is inside its window
        auto consider = [&](const HandState& a_state, Hand a_hand) {
            auto ticks = a_state.pressedAt.load();
            if (ticks == 0) {
                return false;
            }
            float ago = SecondsSince(ticks);
            bool ward = a_state.ward.load();
            float window = config.counterWindow + (ward ? config.wardBonusWindow : 0.0f);
            bool inWindow = ago <= window;
            if (best.secondsAgo < 0.0f || ago < best.secondsAgo) {  // keep the more recent press
                best.hand = a_hand;
                best.ward = ward;
                best.secondsAgo = ago;
                best.inWindow = inWindow;
            }
            return inWindow;
        };

        bool left = consider(m_left, Hand::kLeft);
        bool right = consider(m_right, Hand::kRight);
        best.bothHands = left && right;
        return best;
    }

    void CounterInput::RecordPress(Hand a_hand, bool a_ward)
    {
        auto* state = a_hand == Hand::kLeft ? &m_left : a_hand == Hand::kRight ? &m_right : nullptr;
        if (!state) {
            return;
        }
        state->ward.store(a_ward);
        state->pressedAt.store(Clock::now().time_since_epoch().count());
    }

    void CounterInput::SetHeld(Hand a_hand, bool a_held)
    {
        if (a_hand == Hand::kLeft) {
            m_left.held.store(a_held);
        } else if (a_hand == Hand::kRight) {
            m_right.held.store(a_held);
        }
    }

    bool CounterInput::IsHeld(Hand a_hand) const
    {
        switch (a_hand) {
        case Hand::kLeft:
            return m_left.held.load();
        case Hand::kRight:
            return m_right.held.load();
        default:
            return false;
        }
    }

    void CounterInput::Consume()
    {
        m_left.pressedAt.store(0);
        m_right.pressedAt.store(0);
    }
}
